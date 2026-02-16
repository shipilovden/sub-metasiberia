/*=====================================================================
AdminHandlers.cpp
-----------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "AdminHandlers.h"


#include "RequestInfo.h"
#include "Response.h"
#include "Escaping.h"
#include "RuntimeCheck.h"
#include "ResponseUtils.h"
#include "WebServerResponseUtils.h"
#include "LoginHandlers.h"
#include "WorldHandlers.h"
#include "WebDataStore.h"
#include "../server/ServerWorldState.h"
#include <IncludeXXHash.h>
#include <ConPrint.h>
#include <Exception.h>
#include <Lock.h>
#include <Parser.h>
#include <Escaping.h>
#include <StringUtils.h>
#include <Base64.h>
#include <CryptoRNG.h>
#include <ContainerUtils.h>
#include <algorithm>


namespace AdminHandlers
{

static std::string getUserNameForID(ServerAllWorldsState& world_state, const UserID& user_id)
{
	const auto res = world_state.user_id_to_users.find(user_id);
	return (res != world_state.user_id_to_users.end()) ? res->second->name : std::string();
}


static std::string shortHashForWebFile(const Reference<WebDataStoreFile>& file)
{
	if(file.isNull())
		return "n/a";

	const uint64 hash = XXH64(file->uncompressed_data.data(), file->uncompressed_data.size(), /*seed=*/1);
	return toHexString(hash).substr(0, 8);
}


static bool chatbotMatchesFilters(const std::string& world_name_filter, const std::string& username_filter, const std::string& chatbot_world_name, const std::string& owner_name)
{
	if(!world_name_filter.empty() && (chatbot_world_name != world_name_filter))
		return false;
	if(!username_filter.empty() && (owner_name != username_filter))
		return false;
	return true;
}


static bool getUserIDForUsername(ServerAllWorldsState& world_state, const std::string& username, UserID& user_id_out)
{
	const auto user_res = world_state.name_to_users.find(username);
	if(user_res == world_state.name_to_users.end())
		return false;

	user_id_out = user_res->second->id;
	return true;
}


static bool parseCommaSeparatedUsernamesToIDs(ServerAllWorldsState& world_state, const std::string& usernames_csv, std::vector<UserID>& user_ids_out, std::string& error_out)
{
	const std::vector<std::string> username_fields = ::split(usernames_csv, ',');
	for(size_t i=0; i<username_fields.size(); ++i)
	{
		const std::string username = stripHeadAndTailWhitespace(username_fields[i]);
		if(username.empty())
			continue;

		UserID user_id;
		if(!getUserIDForUsername(world_state, username, user_id))
		{
			error_out = "Could not find user '" + username + "'.";
			return false;
		}

		if(!ContainerUtils::contains(user_ids_out, user_id))
			user_ids_out.push_back(user_id);
	}

	return true;
}


static std::string generateReadablePassword()
{
	const size_t NUM_RANDOM_BYTES = 12;
	uint8 random_bytes[NUM_RANDOM_BYTES];
	CryptoRNG::getRandomBytes(random_bytes, NUM_RANDOM_BYTES);

	std::string raw;
	Base64::encode(random_bytes, NUM_RANDOM_BYTES, raw);
	std::string password;
	password.reserve(raw.size());

	for(size_t i=0; i<raw.size(); ++i)
	{
		const char c = raw[i];
		if(c == '=')
			continue;
		password.push_back((c == '+' || c == '/') ? '_' : c);
	}

	if(password.size() > 18)
		password.resize(18);
	if(password.size() < 12)
		password += "M7p";

	return password;
}


static std::string parseWorldNameFromSubURL(const std::string& sub_url)
{
	if(!hasPrefix(sub_url, "sub://"))
		return sub_url;

	const size_t host_start = 6; // after "sub://"
	const size_t slash_pos = sub_url.find('/', host_start);
	if(slash_pos == std::string::npos)
		return ""; // URL points to root world.

	const size_t world_start = slash_pos + 1;
	const size_t query_pos = sub_url.find('?', world_start);
	const std::string world_part = (query_pos == std::string::npos) ? sub_url.substr(world_start) : sub_url.substr(world_start, query_pos - world_start);
	return web::Escaping::URLUnescape(world_part);
}


static std::string normaliseWorldNameField(const std::string& world_field)
{
	const std::string trimmed = stripHeadAndTailWhitespace(world_field);
	if(trimmed.empty() || trimmed == "/" || trimmed == "root" || trimmed == "main")
		return "";

	if(hasPrefix(trimmed, "sub://"))
		return parseWorldNameFromSubURL(trimmed);

	if(hasPrefix(trimmed, "http://") || hasPrefix(trimmed, "https://"))
	{
		const size_t world_path_pos = trimmed.find("/world/");
		if(world_path_pos != std::string::npos)
		{
			const size_t world_start = world_path_pos + 7;
			const size_t query_pos = trimmed.find('?', world_start);
			const std::string world_part = (query_pos == std::string::npos) ? trimmed.substr(world_start) : trimmed.substr(world_start, query_pos - world_start);
			return web::Escaping::URLUnescape(world_part);
		}

		const size_t world_query_pos = trimmed.find("world=");
		if(world_query_pos != std::string::npos)
		{
			const size_t world_start = world_query_pos + 6;
			const size_t amp_pos = trimmed.find('&', world_start);
			const std::string world_part = (amp_pos == std::string::npos) ? trimmed.substr(world_start) : trimmed.substr(world_start, amp_pos - world_start);
			return web::Escaping::URLUnescape(world_part);
		}
	}

	return trimmed;
}


static double parseDoubleFieldForParcelCreation(const web::RequestInfo& request, const std::string& field_name)
{
	std::string s = stripHeadAndTailWhitespace(request.getPostField(field_name).str());
	if(s.empty())
		throw glare::Exception("Missing field '" + field_name + "'.");
	std::replace(s.begin(), s.end(), ',', '.');
	return stringToDouble(s);
}


static std::string featureFlagNameForBit(const uint64 bitflag)
{
	if(bitflag == ServerAllWorldsState::SERVER_SCRIPT_EXEC_FEATURE_FLAG) return "SERVER_SCRIPT_EXEC";
	if(bitflag == ServerAllWorldsState::LUA_HTTP_REQUESTS_FEATURE_FLAG) return "LUA_HTTP_REQUESTS";
	if(bitflag == ServerAllWorldsState::DO_WORLD_MAINTENANCE_FEATURE_FLAG) return "DO_WORLD_MAINTENANCE";
	if(bitflag == ServerAllWorldsState::CHATBOTS_FEATURE_FLAG) return "CHATBOTS";
	if(bitflag == ServerAllWorldsState::ALLOW_PERSONAL_WORLD_PARCEL_CREATION_FEATURE_FLAG) return "ALLOW_PERSONAL_WORLD_PARCEL_CREATION";
	return "UNKNOWN_FLAG";
}


std::string sharedAdminHeader(ServerAllWorldsState& world_state, const web::RequestInfo& request_info)
{
	std::string page_out = WebServerResponseUtils::standardHeader(world_state, request_info, /*page title=*/"Admin");

	page_out += "<p><a href=\"/admin\">Main admin page</a> | <a href=\"/admin_users\">Users</a> | <a href=\"/admin_parcels\">Parcels</a> | ";
	page_out += "<a href=\"/admin_parcel_auctions\">Parcel Auctions</a> | <a href=\"/admin_orders\">Orders</a> | <a href=\"/admin_sub_eth_transactions\">Eth Transactions</a> | <a href=\"/admin_map\">Map</a> | ";
	page_out += "<a href=\"/admin_news_posts\">News Posts</a> | <a href=\"/admin_lod_chunks\">LOD Chunks</a> | <a href=\"/admin_worlds\">Worlds</a> | <a href=\"/admin_chatbots\">ChatBots</a> </p>";

	return page_out;
}


void renderMainAdminPage(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request_info))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	std::string page_out = sharedAdminHeader(world_state, request_info);

	page_out += "<p>Welcome!</p><br/><br/>";
	page_out += "<h2>Superadmin parcel tools</h2>";
	page_out += "<p><a href=\"/admin_parcels\">Open parcels admin page</a></p>";

	page_out += "<form action=\"/admin_create_parcel\" method=\"post\">";
	page_out += "<label for=\"admin-create-root-owner\">Owner username</label><br/>";
	page_out += "<input id=\"admin-create-root-owner\" type=\"text\" name=\"owner_username\" value=\"\" title=\"Username that will own the new root-world parcel\"><br/>";
	page_out += "<label for=\"admin-create-root-editors\">Editors usernames (comma-separated)</label><br/>";
	page_out += "<input id=\"admin-create-root-editors\" type=\"text\" name=\"writer_usernames\" value=\"\" title=\"Comma-separated usernames with edit rights\"><br/>";
	page_out += "<label for=\"admin-create-root-world\">World (name or link, empty=root)</label><br/>";
	page_out += "<input id=\"admin-create-root-world\" type=\"text\" name=\"world_name\" value=\"\" title=\"World name, /world/... URL, or sub://... link\"><br/>";
	page_out += "<label for=\"admin-create-origin-x\">Origin X</label><br/>";
	page_out += "<input id=\"admin-create-origin-x\" type=\"text\" name=\"origin_x\" value=\"0\"><br/>";
	page_out += "<label for=\"admin-create-origin-y\">Origin Y</label><br/>";
	page_out += "<input id=\"admin-create-origin-y\" type=\"text\" name=\"origin_y\" value=\"0\"><br/>";
	page_out += "<label for=\"admin-create-size-x\">Size X</label><br/>";
	page_out += "<input id=\"admin-create-size-x\" type=\"text\" name=\"size_x\" value=\"16\"><br/>";
	page_out += "<label for=\"admin-create-size-y\">Size Y</label><br/>";
	page_out += "<input id=\"admin-create-size-y\" type=\"text\" name=\"size_y\" value=\"16\"><br/>";
	page_out += "<label for=\"admin-create-min-z\">Min Z</label><br/>";
	page_out += "<input id=\"admin-create-min-z\" type=\"text\" name=\"min_z\" value=\"-1\"><br/>";
	page_out += "<label for=\"admin-create-max-z\">Max Z</label><br/>";
	page_out += "<input id=\"admin-create-max-z\" type=\"text\" name=\"max_z\" value=\"4\"><br/>";
	page_out += "<input type=\"submit\" value=\"Create new parcel\" title=\"Create a parcel using configured world and coordinates\" onclick=\"return confirm('Create parcel with these settings?');\" >";
	page_out += "</form>";

	page_out += "<form onsubmit=\"window.location='/parcel/' + document.getElementById('admin-open-parcel-id').value; return false;\">";
	page_out += "parcel id: <input type=\"number\" id=\"admin-open-parcel-id\" value=\"1\" min=\"1\"> ";
	page_out += "<input type=\"submit\" value=\"Open parcel page (edit tools)\">";
	page_out += "</form>";

	page_out += "<form onsubmit=\"window.location='/admin_set_parcel_owner/' + document.getElementById('admin-set-owner-parcel-id').value; return false;\">";
	page_out += "parcel id: <input type=\"number\" id=\"admin-set-owner-parcel-id\" value=\"1\" min=\"1\"> ";
	page_out += "<input type=\"submit\" value=\"Open set owner form\">";
	page_out += "</form>";
	page_out += "<hr/>";

	{ // Lock scope
		Lock lock(world_state.mutex);
		if(world_state.server_admin_message.empty())
		{
			page_out += "<p>No server admin message set.</p>";
		}
		else
		{
			page_out += "<p>Current server admin message: '"+ web::Escaping::HTMLEscape(world_state.server_admin_message) + "'.</p>";
		}
	} // End Lock scope

	page_out += "<form action=\"/admin_set_server_admin_message_post\" method=\"post\">";
	page_out += "<input type=\"text\" name=\"msg\" value=\"\">";
	page_out += "<input type=\"submit\" value=\"Set server admin message\" onclick=\"return confirm('Are you sure you want set the server admin message?');\" >";
	page_out += "</form>";

	{ // Lock scope
		Lock lock(world_state.mutex);

		if(world_state.read_only_mode)
			page_out += "<p>Server is in read-only mode!</p>";
		else
			page_out += "<p>Server is not in read-only mode.</p>";

		page_out += "<form action=\"/admin_set_read_only_mode_post\" method=\"post\">";
		page_out += "<input type=\"number\" name=\"read_only_mode\" value=\"" + toString(world_state.read_only_mode ? 1 : 0) + "\">";
		page_out += "<input type=\"submit\" value=\"Set server read-only mode (1 / 0)\" onclick=\"return confirm('Are you sure you want set the server read-only mode?');\" >";
		page_out += "</form>";
	} // End Lock scope

	page_out += "<br/><br/>";
	page_out += "<form action=\"/admin_force_dyn_tex_update_post\" method=\"post\">";
	page_out += "<input type=\"submit\" value=\"Force dynamic texture update checker to run\" onclick=\"return confirm('Are you sure you want to force the dynamic texture update checker to run?');\" >";
	page_out += "</form>";

	page_out += "<br/><br/>";

	{ // Lock scope
		Lock lock(world_state.mutex);

		const bool script_exec_enabled = BitUtils::isBitSet(world_state.feature_flag_info.feature_flags, ServerAllWorldsState::SERVER_SCRIPT_EXEC_FEATURE_FLAG);

		page_out += "<p>Server-side script execution: " + (script_exec_enabled ? 
			std::string("<span class=\"feature-enabled\">enabled</span>") : std::string("<span class=\"feature-disabled\">disabled</span>")) + 
			"</p>";

		page_out += "<form action=\"/admin_set_feature_flag_post\" method=\"post\">\n";
		page_out += "<input type=\"hidden\" name=\"flag_bit_index\" value=\"" + toString(BitUtils::highestSetBitIndex(ServerAllWorldsState::SERVER_SCRIPT_EXEC_FEATURE_FLAG)) + "\">\n";
		page_out += "<input type=\"number\" name=\"new_value\" value=\"" + toString(script_exec_enabled ? 1 : 0) + "\">\n";
		page_out += "<input type=\"submit\" value=\"Set server-side script execution enabled (1 / 0)\">\n";
		page_out += "</form>";


		const bool lua_http_enabled = BitUtils::isBitSet(world_state.feature_flag_info.feature_flags, ServerAllWorldsState::LUA_HTTP_REQUESTS_FEATURE_FLAG);

		page_out += "<p>Lua HTTP requests: " + (lua_http_enabled ? 
			std::string("<span class=\"feature-enabled\">enabled</span>") : std::string("<span class=\"feature-disabled\">disabled</span>")) + 
			"</p>";

		page_out += "<form action=\"/admin_set_feature_flag_post\" method=\"post\">";
		page_out += "<input type=\"hidden\" name=\"flag_bit_index\" value=\"" + toString(BitUtils::highestSetBitIndex(ServerAllWorldsState::LUA_HTTP_REQUESTS_FEATURE_FLAG)) + "\">";
		page_out += "<input type=\"number\" name=\"new_value\" value=\"" + toString(lua_http_enabled ? 1 : 0) + "\">";
		page_out += "<input type=\"submit\" value=\"Set Lua HTTP requests enabled (1 / 0)\" onclick=\"return confirm('Are you sure you want to change Lua HTTP requests?');\" >";
		page_out += "</form>";


		const bool do_world_maintenance_enabled = BitUtils::isBitSet(world_state.feature_flag_info.feature_flags, ServerAllWorldsState::DO_WORLD_MAINTENANCE_FEATURE_FLAG);

		page_out += "<p>Do world maintenance: " + (do_world_maintenance_enabled ? 
			std::string("<span class=\"feature-enabled\">enabled</span>") : std::string("<span class=\"feature-disabled\">disabled</span>")) + 
			"</p>";

		page_out += "<form action=\"/admin_set_feature_flag_post\" method=\"post\">";
		page_out += "<input type=\"hidden\" name=\"flag_bit_index\" value=\"" + toString(BitUtils::highestSetBitIndex(ServerAllWorldsState::DO_WORLD_MAINTENANCE_FEATURE_FLAG)) + "\">";
		page_out += "<input type=\"number\" name=\"new_value\" value=\"" + toString(do_world_maintenance_enabled ? 1 : 0) + "\">";
		page_out += "<input type=\"submit\" value=\"Do world maintenance (e.g. remove old bikes) (1 / 0)\" onclick=\"return confirm('Are you sure you want to change world maintenance?');\" >";
		page_out += "</form>";


		const bool chatbots_enabled = BitUtils::isBitSet(world_state.feature_flag_info.feature_flags, ServerAllWorldsState::CHATBOTS_FEATURE_FLAG);

		page_out += "<p>Run chatbots: " + (chatbots_enabled ? 
			std::string("<span class=\"feature-enabled\">enabled</span>") : std::string("<span class=\"feature-disabled\">disabled</span>")) + 
			"</p>";

		page_out += "<form action=\"/admin_set_feature_flag_post\" method=\"post\">";
		page_out += "<input type=\"hidden\" name=\"flag_bit_index\" value=\"" + toString(BitUtils::highestSetBitIndex(ServerAllWorldsState::CHATBOTS_FEATURE_FLAG)) + "\">";
		page_out += "<input type=\"hidden\" name=\"new_value\" value=\"" + toString(chatbots_enabled ? 0 : 1) + "\">";
		page_out += "<input type=\"submit\" value=\"" + std::string(chatbots_enabled ? "Disable chatbots" : "Enable chatbots") + "\" onclick=\"return confirm('Are you sure you want to change chatbots?');\" >";
		page_out += "</form>";

		page_out += "<p>Create parcels in user/personal worlds: <span class=\"feature-enabled\">superadmin only</span>.</p>";
	}

	{
		std::string fragments_dir = "n/a";
		std::string public_files_dir = "n/a";
		std::string webclient_dir = "n/a";
		std::string main_css_hash = "n/a";
		std::string logo_small_hash = "n/a";
		std::string logo_main_hash = "n/a";

		WebDataStore* data_store = world_state.web_data_store;
		if(data_store)
		{
			{
				Lock lock(data_store->hash_mutex);
				if(!data_store->main_css_hash.empty())
					main_css_hash = data_store->main_css_hash;
			}

			{
				Lock lock(data_store->mutex);
				fragments_dir = data_store->fragments_dir;
				public_files_dir = data_store->public_files_dir;
				webclient_dir = data_store->webclient_dir;

				auto logo_small_res = data_store->public_files.find("logo_small.png");
				if(logo_small_res != data_store->public_files.end())
					logo_small_hash = shortHashForWebFile(logo_small_res->second);

				auto logo_main_res = data_store->public_files.find("logo_main_page.png");
				if(logo_main_res != data_store->public_files.end())
					logo_main_hash = shortHashForWebFile(logo_main_res->second);
			}
		}

		page_out += "<br/><hr/>";
		page_out += "<h2>Web assets status</h2>";
		page_out += "<div>fragments_dir: <code>" + web::Escaping::HTMLEscape(fragments_dir) + "</code></div>";
		page_out += "<div>public_files_dir: <code>" + web::Escaping::HTMLEscape(public_files_dir) + "</code></div>";
		page_out += "<div>webclient_dir: <code>" + web::Escaping::HTMLEscape(webclient_dir) + "</code></div>";
		page_out += "<div>main.css hash: <code>" + web::Escaping::HTMLEscape(main_css_hash) + "</code></div>";
		page_out += "<div>logo_small.png hash: <code>" + web::Escaping::HTMLEscape(logo_small_hash) + "</code></div>";
		page_out += "<div>logo_main_page.png hash: <code>" + web::Escaping::HTMLEscape(logo_main_hash) + "</code></div>";

		page_out += "<form action=\"/admin_reload_web_data_post\" method=\"post\">";
		page_out += "<input type=\"submit\" value=\"Force reload web data now\" onclick=\"return confirm('Reload web data from disk now?');\" >";
		page_out += "</form>";
	}

	{
		Lock lock(world_state.mutex);
		page_out += "<br/><hr/>";
		page_out += "<h2>Admin audit log</h2>";
		page_out += "<div>Recent actions (newest first):</div>";

		int num_shown = 0;
		for(auto it = world_state.admin_action_log.rbegin(); it != world_state.admin_action_log.rend() && num_shown < 100; ++it, ++num_shown)
		{
			const AdminActionLogEntry& e = *it;
			page_out += "<div>" + e.time.RFC822FormatedString() + " | user " + e.user_id.toString() + " (" + web::Escaping::HTMLEscape(e.username) + ") | " +
				web::Escaping::HTMLEscape(e.action) + "</div>";
		}
	}

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page_out);
}


void renderUsersPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	std::string page_out = sharedAdminHeader(world_state, request);

	{ // Lock scope
		Lock lock(world_state.mutex);

		page_out += "<h2>Users</h2>\n";
		page_out += "<div class=\"msb-table-wrap\">";
		page_out += "<table class=\"msb-table\" aria-label=\"Users table\">";
		page_out += "<thead><tr><th>ID</th><th>Username</th><th>Email</th><th>Joined</th><th>ETH address</th></tr></thead>";
		page_out += "<tbody>";

		for(auto it = world_state.user_id_to_users.begin(); it != world_state.user_id_to_users.end(); ++it)
		{
			const User* user = it->second.ptr();
			page_out += "<tr>";
			page_out += "<td><a href=\"/admin_user/" + user->id.toString() + "\">" + user->id.toString() + "</a></td>";
			page_out += "<td>" + web::Escaping::HTMLEscape(user->name) + "</td>";
			page_out += "<td>" + web::Escaping::HTMLEscape(user->email_address) + "</td>";
			page_out += "<td>" + web::Escaping::HTMLEscape(user->created_time.timeAgoDescription()) + "</td>";
			page_out += "<td><span class=\"eth-address\">" + web::Escaping::HTMLEscape(user->controlled_eth_address) + "</span></td>";
			page_out += "</tr>\n";
		}
		page_out += "</tbody></table></div>";
	} // End Lock scope

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page_out);
}


void renderAdminUserPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	// Parse user id from request path
	Parser parser(request.path);
	if(!parser.parseString("/admin_user/"))
		throw glare::Exception("Failed to parse /admin_user/");

	uint32 user_id;
	if(!parser.parseUnsignedInt(user_id))
		throw glare::Exception("Failed to parse user id");


	std::string page_out = sharedAdminHeader(world_state, request);

	{ // Lock scope
		Lock lock(world_state.mutex);
		const User* logged_in_admin = LoginHandlers::getLoggedInUser(world_state, request);
		if(logged_in_admin)
		{
			const std::string msg = world_state.getAndRemoveUserWebMessage(logged_in_admin->id);
			if(!msg.empty())
				page_out += "<div class=\"msg\">" + web::Escaping::HTMLEscape(msg) + "</div>";
		}

		page_out += "<h2>User " + toString(user_id) + "</h2>\n";

		auto res = world_state.user_id_to_users.find(UserID(user_id));
		if(res == world_state.user_id_to_users.end())
		{
			page_out += "No user with that id found.";
		}
		else
		{
			const User* user = res->second.ptr();

			page_out += "<p>\n";
			page_out += 
				"created_time: " + user->created_time.RFC822FormatedString() + "(" + user->created_time.timeAgoDescription() + ")<br/>" +
				"name: " + web::Escaping::HTMLEscape(user->name) + "<br/>" +
				"email_address: " + web::Escaping::HTMLEscape(user->email_address) + "<br/>" +
				"controlled_eth_address: " + web::Escaping::HTMLEscape(user->controlled_eth_address) + "<br/>" +
				"avatar model_url: " + web::Escaping::HTMLEscape(toStdString(user->avatar_settings.model_url)) + "<br/>" +
				"flags: " + toString(user->flags) + "<br/>";

			for(size_t i=0; i<user->password_resets.size(); ++i)
			{
				page_out += "Password reset " + toString(i) + ": created_time: " + 
					user->password_resets[i].created_time.timeAgoDescription() + "<br/>";
			}

			page_out += "</p>    \n";

			page_out += "<form action=\"/admin_set_user_as_world_gardener_post\" method=\"post\">";
			page_out += "<input type=\"hidden\" name=\"user_id\" value=\"" + toString(user_id) + "\">";
			page_out += "<input type=\"number\" name=\"gardener\" value=\"" + toString(BitUtils::isBitSet(user->flags, User::WORLD_GARDENER_FLAG) ? 1 : 0) + "\">";
			page_out += "<input type=\"submit\" value=\"Set as world gardener (1 / 0)\" onclick=\"return confirm('Are you sure you want set as world gardener?');\" >";
			page_out += "</form>";

			page_out += "</p>    \n";

			page_out += "<form action=\"/admin_set_user_allow_dyn_tex_update_post\" method=\"post\">";
			page_out += "<input type=\"hidden\" name=\"user_id\" value=\"" + toString(user_id) + "\">";
			page_out += "<input type=\"number\" name=\"allow\" value=\"" + toString(BitUtils::isBitSet(user->flags, User::ALLOW_DYN_TEX_UPDATE_CHECKING) ? 1 : 0) + "\">";
			page_out += "<input type=\"submit\" value=\"Allow user to do dynamic texture update checking (1 / 0)\" onclick=\"return confirm('Are you sure you want to allow user to do dynamic texture update checking?');\" >";
			page_out += "</form>";

			page_out += "<hr/>";
			page_out += "<h3>Reset user password (superadmin)</h3>";
			page_out += "<div class=\"field-description\">Sets a new password without old password. Generated password is shown once after submit.</div>";
			page_out += "<form action=\"/admin_reset_user_password_post\" method=\"post\">";
			page_out += "<input type=\"hidden\" name=\"user_id\" value=\"" + toString(user_id) + "\">";
			page_out += "<label for=\"admin-new-password\">Manual new password (optional)</label><br/>";
			page_out += "<input id=\"admin-new-password\" type=\"text\" name=\"new_password\" value=\"\" title=\"Leave empty to auto-generate\"><br/>";
			page_out += "<label><input type=\"checkbox\" name=\"generate\" value=\"1\" checked=\"checked\"> Auto-generate secure password if empty</label><br/>";
			page_out += "<input type=\"submit\" value=\"Reset password now\" title=\"Reset this user's password\" onclick=\"return confirm('Reset password for this user now?');\" >";
			page_out += "</form>";
		}
	} // End Lock scope

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page_out);
}


void renderParcelsPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	std::string page_out = sharedAdminHeader(world_state, request);

	{ // Lock scope
		Lock lock(world_state.mutex);

		page_out += "<h2>Root world Parcels</h2>\n";

		//-----------------------
		page_out += "<hr/>";
		page_out += "<form action=\"/admin_regenerate_multiple_parcel_screenshots\" method=\"post\">";
		page_out += "start parcel id: <input type=\"number\" name=\"start_parcel_id\" value=\"" + toString(0) + "\"><br/>";
		page_out += "end parcel id: <input type=\"number\" name=\"end_parcel_id\" value=\"" + toString(10) + "\"><br/>";
		page_out += "<input type=\"submit\" value=\"Regenerate/recreate parcel screenshots\" onclick=\"return confirm('Are you sure you want to recreate parcel screenshots?');\" >";
		page_out += "</form>";
		page_out += "<hr/>";
		//-----------------------

		//-----------------------
		page_out += "<hr/>";
		page_out += "<form action=\"/admin_create_parcel\" method=\"post\">";
		page_out += "<div class=\"field-description\">Configure where and how the parcel should be created.</div>";
		page_out += "World (name or link): <input type=\"text\" name=\"world_name\" value=\"\" title=\"World name, /world/... URL, or sub://... link. Leave empty for root world\"><br/>";
		page_out += "Owner username: <input type=\"text\" name=\"owner_username\" value=\"\" title=\"Username that will own the new parcel\"><br/>";
		page_out += "Editors usernames: <input type=\"text\" name=\"writer_usernames\" value=\"\" title=\"Comma-separated usernames with edit rights\"><br/>";
		page_out += "Origin X: <input type=\"text\" name=\"origin_x\" value=\"0\" title=\"Parcel bottom-left X coordinate\"> ";
		page_out += "Origin Y: <input type=\"text\" name=\"origin_y\" value=\"0\" title=\"Parcel bottom-left Y coordinate\"><br/>";
		page_out += "Size X: <input type=\"text\" name=\"size_x\" value=\"16\" title=\"Parcel width on X axis\"> ";
		page_out += "Size Y: <input type=\"text\" name=\"size_y\" value=\"16\" title=\"Parcel width on Y axis\"><br/>";
		page_out += "Min Z: <input type=\"text\" name=\"min_z\" value=\"-1\" title=\"Minimum parcel build height\"> ";
		page_out += "Max Z: <input type=\"text\" name=\"max_z\" value=\"4\" title=\"Maximum parcel build height\"><br/>";
		page_out += "<input type=\"submit\" value=\"Create new parcel\" title=\"Create a parcel in selected world using these coordinates\" onclick=\"return confirm('Create parcel with these settings?');\" >";
		page_out += "</form>";
		page_out += "<hr/>";
		//-----------------------



		Reference<ServerWorldState> root_world = world_state.getRootWorldState();

		for(auto it = root_world->parcels.begin(); it != root_world->parcels.end(); ++it)
		{
			const Parcel* parcel = it->second.ptr();

			// Look up owner
			std::string owner_username;
			auto user_res = world_state.user_id_to_users.find(parcel->owner_id);
			if(user_res == world_state.user_id_to_users.end())
				owner_username = "[No user found]";
			else
				owner_username = user_res->second->name;

			page_out += "<p>\n";
			page_out += "<a href=\"/parcel/" + parcel->id.toString() + "\">Parcel " + parcel->id.toString() + "</a><br/>" +
				"owner: " + web::Escaping::HTMLEscape(owner_username) + "<br/>" +
				"description: " + web::Escaping::HTMLEscape(parcel->description) + "<br/>" +
				"created " + parcel->created_time.timeAgoDescription();

			// Get any auctions for parcel
			page_out += "<div>    \n";
			for(size_t i=0; i<parcel->parcel_auction_ids.size(); ++i)
			{
				const uint32 auction_id = parcel->parcel_auction_ids[i];
				auto auction_res = world_state.parcel_auctions.find(auction_id);
				if(auction_res != world_state.parcel_auctions.end())
				{
					const ParcelAuction* auction = auction_res->second.ptr();
					if(auction->auction_state == ParcelAuction::AuctionState_ForSale)
						page_out += " <a href=\"/parcel_auction/" + toString(auction->id) + "\">Auction " + toString(auction->id) + ": For sale</a><br/>";
					else if(auction->auction_state == ParcelAuction::AuctionState_Sold)
						page_out += " <a href=\"/parcel_auction/" + toString(auction->id) + "\">Auction " + toString(auction->id) + ": Parcel sold.</a><br/>";
				}
			}
			page_out += "</div>    \n";

			page_out += " <a href=\"/admin_create_parcel_auction/" + parcel->id.toString() + "\">Create auction</a>";
			page_out += " | <a href=\"/parcel/" + parcel->id.toString() + "\">Edit parcel</a>";
			page_out += " | <a href=\"/admin_set_parcel_owner/" + parcel->id.toString() + "\">Set owner</a>";
			page_out += " | <a href=\"/add_parcel_writer?parcel_id=" + parcel->id.toString() + "\">Manage editors</a>";
			page_out += " | <form action=\"/admin_delete_parcel\" method=\"post\" style=\"display: inline;\">";
			page_out += "<input type=\"hidden\" name=\"parcel_id\" value=\"" + parcel->id.toString() + "\">";
			page_out += "<input type=\"submit\" value=\"Delete parcel\" onclick=\"return confirm('Are you sure you want to delete parcel " + parcel->id.toString() + "?');\" >";
			page_out += "</form>";

			page_out += "</p>\n";
			page_out += "<br/>  \n";
		}
	} // End Lock scope

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page_out);
}


void renderParcelAuctionsPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	std::string page_out = sharedAdminHeader(world_state, request);

	{ // Lock scope
		Lock lock(world_state.mutex);

		page_out += "<h2>Parcel auctions</h2>\n";

		for(auto it = world_state.parcel_auctions.begin(); it != world_state.parcel_auctions.end(); ++it)
		{
			const ParcelAuction* auction = it->second.ptr();

			page_out += "<p>\n";
			page_out += "<a href=\"/admin_parcel_auction/" + toString(auction->id) + "\">Parcel Auction " + toString(auction->id) + "</a><br/>" +
				"parcel: <a href=\"/parcel/" + auction->parcel_id.toString() + "\">" + auction->parcel_id.toString() + "</a><br/>" + 
				"state: ";

			if(auction->auction_state == ParcelAuction::AuctionState_ForSale)
			{
				page_out += "for-sale";
				if(!auction->currentlyForSale())
					page_out += " [Expired]";
			}
			else if(auction->auction_state == ParcelAuction::AuctionState_Sold)
				page_out += "sold";
			//else if(auction->auction_state == ParcelAuction::AuctionState_NotSold)
			//	page_out += "not-sold";
			page_out += "<br/>";

			const bool order_id_valid = auction->order_id != std::numeric_limits<uint64>::max();

			page_out += 
				"start time: " + auction->auction_start_time.RFC822FormatedString() + "(" + auction->auction_start_time.timeDescription() + ")<br/>" + 
				"end time: " + auction->auction_end_time.RFC822FormatedString() + "(" + auction->auction_end_time.timeDescription() + ")<br/>" +
				"start price: " + toString(auction->auction_start_price) + ", end price: " + toString(auction->auction_end_price) + "<br/>" +
				"sold_price: " + toString(auction->sold_price) + "<br/>" +
				"sold time: " + auction->auction_sold_time.RFC822FormatedString() + "(" + auction->auction_sold_time.timeDescription() + ")<br/>" +
				(order_id_valid ? "order#: <a href=\"/admin_order/" + toString(auction->order_id) + "\">" + toString(auction->order_id) + "</a>" : "order: invalid (not set)") + "<br/>" + 
				"num locks: " + toString(auction->auction_locks.size());

			page_out += "</p>\n";
		}
	} // End Lock scope

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page_out);
}


void renderAdminParcelAuctionPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	// Parse parcel auction id from request path
	Parser parser(request.path);
	if(!parser.parseString("/admin_parcel_auction/"))
		throw glare::Exception("Failed to parse /admin_parcel_auction/");

	uint32 auction_id;
	if(!parser.parseUnsignedInt(auction_id))
		throw glare::Exception("Failed to parse auction id");

	std::string page_out = sharedAdminHeader(world_state, request);

	{ // Lock scope
		Lock lock(world_state.mutex);

		auto res = world_state.parcel_auctions.find(auction_id);
		if(res != world_state.parcel_auctions.end())
		{
			const ParcelAuction* auction = res->second.ptr();

			page_out += "<h2>Parcel Auction " + toString(auction_id) + "</h2>\n";

			page_out += "<p>\n";
			page_out += "<a href=\"/parcel_auction/" + toString(auction->id) + "\">Parcel Auction " + toString(auction->id) + "</a><br/>" +
				"parcel: <a href=\"/parcel/" + auction->parcel_id.toString() + "\">" + auction->parcel_id.toString() + "</a><br/>" + 
				"state: ";

			if(auction->auction_state == ParcelAuction::AuctionState_ForSale)
			{
				page_out += "for-sale";
				if(!auction->currentlyForSale())
					page_out += " [Expired]";
			}
			else if(auction->auction_state == ParcelAuction::AuctionState_Sold)
				page_out += "sold";
			//else if(auction->auction_state == ParcelAuction::AuctionState_NotSold)
			//	page_out += "not-sold";
			page_out += "<br/>";

			page_out += 
				"start time: " + auction->auction_start_time.RFC822FormatedString() + "(" + auction->auction_start_time.timeDescription() + ")<br/>" + 
				"end time: " + auction->auction_end_time.RFC822FormatedString() + "(" + auction->auction_end_time.timeDescription() + ")<br/>" +
				"start price: " + toString(auction->auction_start_price) + ", end price: " + toString(auction->auction_end_price) + "<br/>" +
				"sold_price: " + toString(auction->sold_price) + "<br/>" +
				"sold time: " + auction->auction_sold_time.RFC822FormatedString() + "(" + auction->auction_sold_time.timeDescription() + ")<br/>" +
				"order#: <a href=\"/admin_order/" + toString(auction->order_id) + "\">" + toString(auction->order_id) + "</a><br/>";

			if(auction->isLocked())
			{
				page_out += "Locked<br/>";
			}
			else
			{
				page_out += "Not locked<br/>";
			}

			page_out += "<h3>Locks</h3>";

			for(size_t i=0; i<auction->auction_locks.size(); ++i)
			{
				const AuctionLock& auction_lock = auction->auction_locks[i];
				page_out += "<p>";

				// Look up user.
				std::string locker_username;
				auto user_res = world_state.user_id_to_users.find(auction_lock.locking_user_id);
				if(user_res == world_state.user_id_to_users.end())
					locker_username = "[No user found]";
				else
					locker_username = user_res->second->name;


				page_out += "created_time: " + auction_lock.created_time.timeAgoDescription() + "</br>";
				page_out += "lock_duration: " + toString(auction_lock.lock_duration) + " s</br>";
				page_out += "locking_user_id: " + auction_lock.locking_user_id.toString() + " (" + locker_username + ")</br>";

				page_out += "</p>";
			}


			page_out += "</p>\n";
		}
	} // End Lock scope

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page_out);
}


void renderOrdersPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	std::string page_out = sharedAdminHeader(world_state, request);

	{ // Lock scope
		Lock lock(world_state.mutex);

		page_out += "<h2>Orders</h2>\n";

		for(auto it = world_state.orders.begin(); it != world_state.orders.end(); ++it)
		{
			const Order* order = it->second.ptr();

			// Look up user who made the order
			std::string orderer_username;
			auto user_res = world_state.user_id_to_users.find(order->user_id);
			if(user_res == world_state.user_id_to_users.end())
				orderer_username = "[No user found]";
			else
				orderer_username = user_res->second->name;


			page_out += "<p>\n";
			page_out += "<a href=\"/admin_order/" + toString(order->id) + "\">Order " + toString(order->id) + "</a>, " +
				"orderer: " + web::Escaping::HTMLEscape(orderer_username) + "<br/>" +
				"parcel: <a href=\"/parcel/" + order->parcel_id.toString() + "\">" + order->parcel_id.toString() + "</a>, " + "<br/>" +
				"created_time: " + order->created_time.RFC822FormatedString() + "(" + order->created_time.timeAgoDescription() + ")<br/>" +
				"payer_email: " + web::Escaping::HTMLEscape(order->payer_email) + "<br/>" +
				"gross_payment: " + ::toString(order->gross_payment) + "<br/>" +
				"paypal_data: " + web::Escaping::HTMLEscape(order->paypal_data.substr(0, 60)) + "...</br>" +
				"coinbase charge code: " + order->coinbase_charge_code + "</br>" +
				"coinbase charge status: " + order->coinbase_status + "</br>" +
				"confirmed: " + boolToString(order->confirmed);

			page_out += "</p>    \n";
		}
	} // End Lock scope

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page_out);
}


void renderSubEthTransactionsPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	std::string page_out = sharedAdminHeader(world_state, request);

	{ // Lock scope
		Lock lock(world_state.mutex);


		page_out += "<form action=\"/admin_set_min_next_nonce_post\" method=\"post\">";
		page_out += "<input type=\"number\" name=\"min_next_nonce\" value=\"" + toString(world_state.eth_info.min_next_nonce) + "\">";
		page_out += "<input type=\"submit\" value=\"Set min next nonce\" onclick=\"return confirm('Are you sure you want set the min next nonce?');\" >";
		page_out += "</form>";


		page_out += "<h2>Substrata Ethereum Transactions</h2>\n";

		for(auto it = world_state.sub_eth_transactions.begin(); it != world_state.sub_eth_transactions.end(); ++it)
		{
			const SubEthTransaction* trans = it->second.ptr();

			// Look up user who initiated the transaction
			std::string username;
			auto user_res = world_state.user_id_to_users.find(trans->initiating_user_id);
			if(user_res == world_state.user_id_to_users.end())
				username = "[No user found]";
			else
				username = user_res->second->name;

			page_out += "<h3><a href=\"/admin_sub_eth_transaction/" + toString(trans->id) + "\">Transaction " + toString(trans->id) + "</a></h3>";
			page_out += "<p>\n";
			page_out += 
				"initiating user: " + web::Escaping::HTMLEscape(username) + "<br/>" +
				"user_eth_address: <a href=\"https://etherscan.io/address/" + web::Escaping::HTMLEscape(trans->user_eth_address) + "\">" + web::Escaping::HTMLEscape(trans->user_eth_address) + "</a><br/>" +
				"parcel: <a href=\"/parcel/" + trans->parcel_id.toString() + "\">" + trans->parcel_id.toString() + "</a>, " + "<br/>" +
				"created_time: " + trans->created_time.RFC822FormatedString() + "(" + trans->created_time.timeAgoDescription() + ")<br/>" +
				"state: " + web::Escaping::HTMLEscape(SubEthTransaction::statestring(trans->state)) + "<br/>";
			if(trans->state != SubEthTransaction::State_New)
			{
				page_out += "submitted_time: " + trans->submitted_time.RFC822FormatedString() + "(" + trans->submitted_time.timeAgoDescription() + ")<br/>";
				page_out += "txn hash: <a href=\"https://etherscan.io/tx/0x" + trans->transaction_hash.toHexString() + "\">" + web::Escaping::HTMLEscape(trans->transaction_hash.toHexString()) + "</a><br/>";
				page_out += "error msg: " + web::Escaping::HTMLEscape(trans->submission_error_message) + "<br/>";
			}

			page_out +=
				"nonce: " + toString(trans->nonce) + "<br/>";

			page_out += "</p>    \n";

			page_out += "<br/>";
		}
	} // End Lock scope

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page_out);
}


void renderAdminSubEthTransactionPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	// Parse order id from request path
	Parser parser(request.path);
	if(!parser.parseString("/admin_sub_eth_transaction/"))
		throw glare::Exception("Failed to parse /admin_sub_eth_transactions/");

	uint32 transaction_id;
	if(!parser.parseUnsignedInt(transaction_id))
		throw glare::Exception("Failed to parse transaction_id");


	std::string page_out = sharedAdminHeader(world_state, request);

	{ // Lock scope
		Lock lock(world_state.mutex);

		page_out += "<h2>Eth transaction " + toString(transaction_id) + "</h2>\n";

		auto res = world_state.sub_eth_transactions.find(transaction_id);
		if(res == world_state.sub_eth_transactions.end())
		{
			page_out += "No transaction with that id found.";
		}
		else
		{
			const SubEthTransaction* trans = res->second.ptr();

			// Look up user who initiated the transaction
			std::string username;
			auto user_res = world_state.user_id_to_users.find(trans->initiating_user_id);
			if(user_res == world_state.user_id_to_users.end())
				username = "[No user found]";
			else
				username = user_res->second->name;

			page_out += "<h3>Transaction " + toString(trans->id) + "</h3>";
			page_out += "<p>\n";
			page_out += 
				"initiating user: " + web::Escaping::HTMLEscape(username) + "<br/>" +
				"user_eth_address: <a href=\"https://etherscan.io/address/" + web::Escaping::HTMLEscape(trans->user_eth_address) + "\">" + web::Escaping::HTMLEscape(trans->user_eth_address) + "</a><br/>" +
				"parcel: <a href=\"/parcel/" + trans->parcel_id.toString() + "\">" + trans->parcel_id.toString() + "</a>, " + "<br/>" +
				"created_time: " + trans->created_time.RFC822FormatedString() + "(" + trans->created_time.timeAgoDescription() + ")<br/>" +
				"state: " + web::Escaping::HTMLEscape(SubEthTransaction::statestring(trans->state)) + "<br/>";
			if(trans->state != SubEthTransaction::State_New)
			{
				page_out += "submitted_time: " + trans->submitted_time.RFC822FormatedString() + "(" + trans->created_time.timeAgoDescription() + ")<br/>";
				page_out += "txn hash: <a href=\"https://etherscan.io/tx/0x" + trans->transaction_hash.toHexString() + "\">" + web::Escaping::HTMLEscape(trans->transaction_hash.toHexString()) + "</a><br/>";
				page_out += "error msg: " + web::Escaping::HTMLEscape(trans->submission_error_message) + "<br/>";
			}

			page_out +=
				"nonce: " + toString(trans->nonce) + "<br/>";

			page_out += "</p>    \n";

			if(trans->state == SubEthTransaction::State_New)
			{
				page_out += "<form action=\"/admin_delete_transaction_post\" method=\"post\">";
				page_out += "<input type=\"hidden\" name=\"transaction_id\" value=\"" + toString(trans->id) + "\">";
				page_out += "<input type=\"submit\" value=\"Delete transaction\" onclick=\"return confirm('Are you sure you want to delete the transaction?');\" >";
				page_out += "</form>";
			}

			if(trans->state != SubEthTransaction::State_New)
			{
				page_out += "<form action=\"/admin_set_transaction_state_to_new_post\" method=\"post\">";
				page_out += "<input type=\"hidden\" name=\"transaction_id\" value=\"" + toString(trans->id) + "\">";
				page_out += "<input type=\"submit\" value=\"Set transaction state to new\" onclick=\"return confirm('Are you sure you want to set the transaction state to new?');\" >";
				page_out += "</form>";
			}

			if(trans->state != SubEthTransaction::State_Completed)
			{
				page_out += "<form action=\"/admin_set_transaction_state_to_completed_post\" method=\"post\">";
				page_out += "<input type=\"hidden\" name=\"transaction_id\" value=\"" + toString(trans->id) + "\">";
				page_out += "<input type=\"submit\" value=\"Set transaction state to completed\" onclick=\"return confirm('Are you sure you want to set the transaction state to completed?');\" >";
				page_out += "</form>";
			}

			page_out += "<br/>";

			page_out += "<form action=\"/admin_set_transaction_state_hash\" method=\"post\">";
			page_out += "<input type=\"hidden\" name=\"transaction_id\" value=\"" + toString(trans->id) + "\">";
			page_out += "<input type=\"text\" name=\"hash\" value=\"" + trans->transaction_hash.toHexString() + "\">";
			page_out += "<input type=\"submit\" value=\"Set hash\" onclick=\"return confirm('Are you sure you want to update the transaction hash?');\" >";
			page_out += "</form>";

			page_out += "<br/>";

			page_out += "<form action=\"/admin_set_transaction_nonce\" method=\"post\">";
			page_out += "<input type=\"hidden\" name=\"transaction_id\" value=\"" + toString(trans->id) + "\">";
			page_out += "<input type=\"number\" name=\"nonce\" value=\"" + toString(trans->nonce) + "\">";
			page_out += "<input type=\"submit\" value=\"Set nonce\" onclick=\"return confirm('Are you sure you want to update the transaction nonce?');\" >";
			page_out += "</form>";

			page_out += "<br/>";
		}
	} // End Lock scope

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page_out);
}


void renderMapPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	std::string page_out = sharedAdminHeader(world_state, request);

	page_out += "<form action=\"/admin_regen_map_tiles_post\" method=\"post\">";
	page_out += "<input type=\"submit\" value=\"Regen map tiles (mark as not done)\">";
	page_out += "</form>";

	page_out += "<form action=\"/admin_recreate_map_tiles_post\" method=\"post\">";
	page_out += "<input type=\"submit\" value=\"Recreate map tiles\">";
	page_out += "</form>";

	{ // Lock scope
		Lock lock(world_state.mutex);

		page_out += "<h2>Map Info</h2>\n";

		for(auto it = world_state.map_tile_info.info.begin(); it != world_state.map_tile_info.info.end(); ++it)
		{
			Vec3<int> v = it->first;
			const TileInfo& info = it->second;

			page_out += "Tile Coords: " + v.toString();
			if(info.cur_tile_screenshot.nonNull())
				page_out += std::string("  ID: ") + toString(info.cur_tile_screenshot->id) + ", state: " + ((info.cur_tile_screenshot->state == Screenshot::ScreenshotState_notdone) ? "Not done" : "Done");

			page_out += "<br/>";
		}
	} // End Lock scope

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page_out);
}


void renderAdminOrderPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	// Parse order id from request path
	Parser parser(request.path);
	if(!parser.parseString("/admin_order/"))
		throw glare::Exception("Failed to parse /admin_order/");

	uint32 order_id;
	if(!parser.parseUnsignedInt(order_id))
		throw glare::Exception("Failed to parse order id");


	std::string page_out = sharedAdminHeader(world_state, request);

	{ // Lock scope
		Lock lock(world_state.mutex);

		page_out += "<h2>Order " + toString(order_id) + "</h2>\n";

		auto res = world_state.orders.find(order_id);
		if(res == world_state.orders.end())
		{
			page_out += "No order with that id found.";
		}
		else
		{
			const Order* order = res->second.ptr();

			// Look up user who made the order
			std::string orderer_username;
			auto user_res = world_state.user_id_to_users.find(order->user_id);
			if(user_res == world_state.user_id_to_users.end())
				orderer_username = "[No user found]";
			else
				orderer_username = user_res->second->name;


			page_out += "<p>\n";
			page_out += "<a href=\"/admin_order/" + toString(order->id) + "\">Order " + toString(order->id) + "</a>, " +
				"orderer: " + web::Escaping::HTMLEscape(orderer_username) + "<br/>" +
				"parcel: <a href=\"/parcel/" + order->parcel_id.toString() + "\">" + order->parcel_id.toString() + "</a>, " + "<br/>" +
				"created_time: " + order->created_time.RFC822FormatedString() + "(" + order->created_time.timeAgoDescription() + ")<br/>" +
				"payer_email: " + web::Escaping::HTMLEscape(order->payer_email) + "<br/>" +
				"gross_payment: " + ::toString(order->gross_payment) + "<br/>" +
				"currency: " + web::Escaping::HTMLEscape(order->currency) + "<br/>" +
				"paypal/coinbase data: " + web::Escaping::HTMLEscape(order->paypal_data) + "</br>" +
				"coinbase charge code: " + order->coinbase_charge_code + "</br>" +
				"coinbase charge status: " + order->coinbase_status + "</br>" +
				"confirmed: " + boolToString(order->confirmed);

			page_out += "</p>    \n";
		}
	} // End Lock scope

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page_out);
}


void renderAdminNewsPostsPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	std::string page_out = sharedAdminHeader(world_state, request);

	{ // Lock scope
		Lock lock(world_state.mutex);

		page_out += "<h2>News Posts</h2>\n";

		//-----------------------
		page_out += "<hr/>";
		page_out += "<form action=\"/admin_new_news_post\" method=\"post\">";
		page_out += "<input type=\"submit\" value=\"New news post\">";
		page_out += "</form>";
		page_out += "<hr/>";
		//-----------------------

		for(auto it = world_state.news_posts.begin(); it != world_state.news_posts.end(); ++it)
		{
			const NewsPost* news_post = it->second.ptr();

			// Look up owner
			std::string creator_username;
			auto user_res = world_state.user_id_to_users.find(news_post->creator_id);
			if(user_res == world_state.user_id_to_users.end())
				creator_username = "[No user found]";
			else
				creator_username = user_res->second->name;

			page_out += "<p>\n";
			page_out += "<a href=\"/news_post/" + toString(news_post->id) + "\">News Post " + toString(news_post->id) + "</a><br/>" +
				"creator: " + web::Escaping::HTMLEscape(creator_username) + "<br/>" +
				"content: " + web::Escaping::HTMLEscape(news_post->content) + "<br/>" +
				"created " + news_post->created_time.timeAgoDescription() + "<br/>" +
				"last modified " + news_post->last_modified_time.timeAgoDescription() + "<br/>" + 
				"State: " + NewsPost::stateString(news_post->state) + "<br/>";

			page_out += "</p>\n";
			page_out += "<br/>  \n";
		}
	} // End Lock scope

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page_out);
}


void renderAdminLODChunksPage(ServerAllWorldsState& all_worlds_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(all_worlds_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	std::string page_out = sharedAdminHeader(all_worlds_state, request);

	{ // Lock scope
		WorldStateLock lock(all_worlds_state.mutex);

		page_out += "<h2>LOD Chunks</h2>\n";

		//-----------------------
		page_out += "<hr/>";
		page_out += "<form action=\"/admin_rebuild_world_lod_chunks\" method=\"post\">";
		page_out += "world name: (Enter 'ALL' to rebuild chunks in all worlds) <input type=\"text\" name=\"world_name\"><br>";
		page_out += "<input type=\"submit\" value=\"Rebuild world LOD chunks\">";
		page_out += "</form>";
		page_out += "<hr/>";
		//-----------------------

		for(auto it = all_worlds_state.world_states.begin(); it != all_worlds_state.world_states.end(); ++it)
		{
			ServerWorldState* world_state = it->second.ptr();

			ServerWorldState::LODChunkMapType& lod_chunks = world_state->getLODChunks(lock);

			if(!lod_chunks.empty())
			{
				page_out += "<h3>World: '" + web::Escaping::HTMLEscape(it->first) + "'</h3>";

				for(auto lod_it = lod_chunks.begin(); lod_it != lod_chunks.end(); ++lod_it)
				{
					const LODChunk* chunk = lod_it->second.ptr();

					page_out += "<div>Coords: " + chunk->coords.toString() + ", <br/> mesh_url: " + web::Escaping::HTMLEscape(toStdString(chunk->getMeshURL())) + ", <br/> combined_array_texture_url: " + web::Escaping::HTMLEscape(toStdString(chunk->combined_array_texture_url)) + 
						"<br/> compressed_mat_info: " + toString(chunk->compressed_mat_info.size()) + " B, <br/> needs_rebuild: " + boolToString(chunk->needs_rebuild) + "</div><br/>";
				}
			}
		}
	} // End Lock scope

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page_out);
}


void renderAdminWorldsPage(ServerAllWorldsState& all_worlds_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(all_worlds_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	std::string page_out = sharedAdminHeader(all_worlds_state, request);

	{ // Lock scope
		WorldStateLock lock(all_worlds_state.mutex);

		page_out += "<h2>Worlds</h2>\n";
		page_out += "<div class=\"msb-table-wrap\">";
		page_out += "<table class=\"msb-table\" aria-label=\"Worlds table\">";
		page_out += "<thead><tr><th>World</th><th>Created (UTC)</th><th>Owner</th><th>Description</th></tr></thead>";
		page_out += "<tbody>";

		for(auto it = all_worlds_state.world_states.begin(); it != all_worlds_state.world_states.end(); ++it)
		{
			ServerWorldState* world_state = it->second.ptr();
			const std::string world_name = world_state->details.name.empty() ? std::string("Main world") : world_state->details.name;
			const std::string owner_name = getUserNameForID(all_worlds_state, world_state->details.owner_id);
			const std::string owner_display = owner_name.empty() ? std::string("[unknown]") : owner_name;
			const std::string description = world_state->details.description.empty() ? std::string("-") : world_state->details.description.substr(0, 400);

			page_out += "<tr>";
			page_out += "<td><a href=\"/world/" + WorldHandlers::URLEscapeWorldName(world_state->details.name) + "\">" + web::Escaping::HTMLEscape(world_name) + "</a></td>";
			page_out += "<td>" + world_state->details.created_time.dayAndTimeStringUTC() + "</td>";
			page_out += "<td>" + web::Escaping::HTMLEscape(owner_display) + "</td>";
			page_out += "<td><i>" + web::Escaping::HTMLEscape(description) + "</i></td>";
			page_out += "</tr>\n";
		}

		page_out += "</tbody></table></div>";
	} // End Lock scope

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page_out);
}


void renderAdminChatBotsPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	const std::string world_name_filter = stripHeadAndTailWhitespace(request.getURLParam("world_name").str());
	const std::string username_filter = stripHeadAndTailWhitespace(request.getURLParam("username").str());

	std::string page_out = sharedAdminHeader(world_state, request);
	page_out += "<h2>ChatBots</h2>\n";

	page_out += "<form action=\"/admin_chatbots\" method=\"get\">";
	page_out += "world name: <input type=\"text\" name=\"world_name\" value=\"" + web::Escaping::HTMLEscape(world_name_filter) + "\"> ";
	page_out += "owner username: <input type=\"text\" name=\"username\" value=\"" + web::Escaping::HTMLEscape(username_filter) + "\"> ";
	page_out += "<input type=\"submit\" value=\"Filter\">";
	page_out += "</form>";

	page_out += "<form action=\"/admin_set_chatbots_disabled_post\" method=\"post\">";
	page_out += "<input type=\"hidden\" name=\"world_name\" value=\"" + web::Escaping::HTMLEscape(world_name_filter) + "\">";
	page_out += "<input type=\"hidden\" name=\"username\" value=\"" + web::Escaping::HTMLEscape(username_filter) + "\">";
	page_out += "<input type=\"hidden\" name=\"new_value\" value=\"1\">";
	page_out += "<input type=\"submit\" value=\"Disable matching chatbots\" onclick=\"return confirm('Disable all matching chatbots?');\" >";
	page_out += "</form>";

	page_out += "<form action=\"/admin_set_chatbots_disabled_post\" method=\"post\">";
	page_out += "<input type=\"hidden\" name=\"world_name\" value=\"" + web::Escaping::HTMLEscape(world_name_filter) + "\">";
	page_out += "<input type=\"hidden\" name=\"username\" value=\"" + web::Escaping::HTMLEscape(username_filter) + "\">";
	page_out += "<input type=\"hidden\" name=\"new_value\" value=\"0\">";
	page_out += "<input type=\"submit\" value=\"Enable matching chatbots\" onclick=\"return confirm('Enable all matching chatbots?');\" >";
	page_out += "</form>";

	page_out += "<form action=\"/admin_delete_chatbots_post\" method=\"post\">";
	page_out += "<input type=\"hidden\" name=\"world_name\" value=\"" + web::Escaping::HTMLEscape(world_name_filter) + "\">";
	page_out += "<input type=\"hidden\" name=\"username\" value=\"" + web::Escaping::HTMLEscape(username_filter) + "\">";
	page_out += "<input type=\"submit\" value=\"Delete matching chatbots\" onclick=\"return confirm('Delete all matching chatbots?');\" >";
	page_out += "</form>";
	page_out += "<hr/>";

	{ // lock scope
		WorldStateLock lock(world_state.mutex);

		int num_total = 0;
		int num_filtered = 0;

		for(auto world_it = world_state.world_states.begin(); world_it != world_state.world_states.end(); ++world_it)
		{
			ServerWorldState* world = world_it->second.ptr();

			for(auto it = world->getChatBots(lock).begin(); it != world->getChatBots(lock).end(); ++it)
			{
				num_total++;
				ChatBot* chatbot = it->second.ptr();

				const std::string owner_name = getUserNameForID(world_state, chatbot->owner_id);
				if(!chatbotMatchesFilters(world_name_filter, username_filter, world_it->first, owner_name))
					continue;

				num_filtered++;

				const bool disabled = BitUtils::isBitSet(chatbot->flags, ChatBot::DISABLED_FLAG);

				page_out += "<div>";
				page_out += "ChatBot <a href=\"/edit_chatbot?chatbot_id=" + toString(chatbot->id) + "\">" + toString(chatbot->id) + "</a>";
				page_out += " | name: <b>" + web::Escaping::HTMLEscape(chatbot->name) + "</b>";
				page_out += " | world: " + (world_it->first.empty() ? std::string("Main world") : web::Escaping::HTMLEscape(world_it->first));
				page_out += " | owner: " + web::Escaping::HTMLEscape(owner_name.empty() ? std::string("[unknown]") : owner_name);
				page_out += " | state: " + std::string(disabled ? "<span class=\"feature-disabled\">disabled</span>" : "<span class=\"feature-enabled\">enabled</span>");

				page_out += " | <form action=\"/admin_set_chatbot_disabled_post\" method=\"post\" style=\"display: inline;\">";
				page_out += "<input type=\"hidden\" name=\"chatbot_id\" value=\"" + toString(chatbot->id) + "\">";
				page_out += "<input type=\"hidden\" name=\"new_value\" value=\"" + toString(disabled ? 0 : 1) + "\">";
				page_out += "<input type=\"submit\" value=\"" + std::string(disabled ? "Enable" : "Disable") + "\" onclick=\"return confirm('Change chatbot state?');\" >";
				page_out += "</form>";

				page_out += " <form action=\"/admin_delete_chatbot_post\" method=\"post\" style=\"display: inline;\">";
				page_out += "<input type=\"hidden\" name=\"chatbot_id\" value=\"" + toString(chatbot->id) + "\">";
				page_out += "<input type=\"submit\" value=\"Delete\" onclick=\"return confirm('Delete chatbot " + toString(chatbot->id) + "?');\" >";
				page_out += "</form>";

				page_out += "</div>";
			}
		}

		page_out += "<hr/>";
		page_out += "<div>Total chatbots: " + toString(num_total) + ", shown by filter: " + toString(num_filtered) + "</div>";
	}

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page_out);
}


void renderCreateParcelAuction(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	// Parse parcel id from request path
	Parser parser(request.path);
	if(!parser.parseString("/admin_create_parcel_auction/"))
		throw glare::Exception("Failed to parse /admin_create_parcel_auction/");

	uint32 id;
	if(!parser.parseUnsignedInt(id))
		throw glare::Exception("Failed to parse parcel id");
	const ParcelID parcel_id(id);


	std::string page_out = WebServerResponseUtils::standardHTMLHeader(*world_state.web_data_store, request, "Sign Up");

	page_out += "<body>";
	page_out += "</head><h1>Create Parcel Auction</h1><body>";

	page_out += "<form action=\"/admin_create_parcel_auction_post\" method=\"post\">";
	page_out += "parcel id: <input type=\"number\" name=\"parcel_id\" value=\"" + parcel_id.toString() + "\"><br>";
	page_out += "auction start time: <input type=\"number\" name=\"auction_start_time\"   value=\"0\"> hours from now<br>";
	page_out += "auction end time:   <input type=\"number\" name=\"auction_end_time\"     value=\"72\"> hours from now<br>";
	page_out += "auction start price: <input type=\"number\" step=\"0.01\" name=\"auction_start_price\" value=\"1000\"> EUR<br/>";
	page_out += "auction end price: <input type=\"number\" step=\"0.01\" name=\"auction_end_price\"     value=\"50\"> EUR<br/>";
	page_out += "<input type=\"submit\" value=\"Create auction\">";
	page_out += "</form>";

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page_out);
}


void createParcelAuctionPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		// Try and log in user
		const uint64 parcel_id              = stringToUInt64(request.getPostField("parcel_id").str());
		const double auction_start_time_hrs = stringToDouble(request.getPostField("auction_start_time").str());
		const double auction_end_time_hrs   = stringToDouble(request.getPostField("auction_end_time").str());
		const double auction_start_price    = stringToDouble(request.getPostField("auction_start_price").str());
		const double auction_end_price      = stringToDouble(request.getPostField("auction_end_price").str());

		{ // Lock scope

			WorldStateLock lock(world_state.mutex);
			const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			runtimeCheck(logged_in_user);

			// Lookup parcel
			const auto res = world_state.getRootWorldState()->parcels.find(ParcelID((uint32)parcel_id));
			if(res != world_state.getRootWorldState()->parcels.end())
			{
				// Found user for username
				Parcel* parcel = res->second.ptr();

				ParcelAuctionRef auction = new ParcelAuction();
				auction->id = (uint32)world_state.parcel_auctions.size() + 1;
				auction->parcel_id = parcel->id;
				auction->auction_state = ParcelAuction::AuctionState_ForSale;
				auction->auction_start_time  = TimeStamp((uint64)(TimeStamp::currentTime().time + auction_start_time_hrs * 3600));
				auction->auction_end_time    = TimeStamp((uint64)(TimeStamp::currentTime().time + auction_end_time_hrs   * 3600));
				auction->auction_start_price = auction_start_price;
				auction->auction_end_price   = auction_end_price;

				world_state.parcel_auctions[auction->id] = auction;


				if(parcel->screenshot_ids.size() < 2)
					throw glare::Exception("Parcel does not have at least 2 screenshots already");

				auction->screenshot_ids = parcel->screenshot_ids;

				// Make new screenshot (request) for parcel auction

				/*
				uint64 next_shot_id = world_state.getNextScreenshotUID();

				// Close-in screenshot
				{
					ScreenshotRef shot = new Screenshot();
					shot->id = next_shot_id++;
					parcel->getScreenShotPosAndAngles(shot->cam_pos, shot->cam_angles);
					shot->width_px = 650;
					shot->highlight_parcel_id = (int)parcel_id;
					shot->created_time = TimeStamp::currentTime();
					shot->state = Screenshot::ScreenshotState_notdone;
					
					world_state.addScreenshotAsDBDirty(shot);
					world_state.screenshots[shot->id] = shot;

					auction->screenshot_ids.push_back(shot->id);
				}
				// Zoomed-out screenshot
				{
					ScreenshotRef shot = new Screenshot();
					shot->id = next_shot_id++;
					parcel->getFarScreenShotPosAndAngles(shot->cam_pos, shot->cam_angles);
					shot->width_px = 650;
					shot->highlight_parcel_id = (int)parcel_id;
					shot->created_time = TimeStamp::currentTime();
					shot->state = Screenshot::ScreenshotState_notdone;
					
					world_state.addScreenshotAsDBDirty(shot);
					world_state.screenshots[shot->id] = shot;

					auction->screenshot_ids.push_back(shot->id);
				}

				if(!request.fuzzing)
					conPrint("Created screenshots for auction");
				*/
				
				parcel->parcel_auction_ids.push_back(auction->id);

				world_state.addParcelAuctionAsDBDirty(auction);
				world_state.getRootWorldState()->addParcelAsDBDirty(parcel, lock);

				web::ResponseUtils::writeRedirectTo(reply_info, "/parcel_auction/" + toString(auction->id));
			}
		} // End lock scope

	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("createParcelAuctionPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void renderSetParcelOwnerPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	// Parse parcel id from request path
	Parser parser(request.path);
	if(!parser.parseString("/admin_set_parcel_owner/"))
		throw glare::Exception("Failed to parse /admin_set_parcel_owner/");

	uint32 id;
	if(!parser.parseUnsignedInt(id))
		throw glare::Exception("Failed to parse parcel id");
	const ParcelID parcel_id(id);


	std::string page_out = WebServerResponseUtils::standardHTMLHeader(*world_state.web_data_store, request, "Sign Up");

	page_out += "<body>";
	page_out += "</head><h1>Set parcel owner</h1><body>";

	{ // Lock scope

		Lock lock(world_state.mutex);

		// Lookup parcel
		const auto res = world_state.getRootWorldState()->parcels.find(parcel_id);
		if(res != world_state.getRootWorldState()->parcels.end())
		{
			// Found user for username
			Parcel* parcel = res->second.ptr();

			// Look up owner
			std::string owner_username;
			auto user_res = world_state.user_id_to_users.find(parcel->owner_id);
			if(user_res == world_state.user_id_to_users.end())
				owner_username = "[No user found]";
			else
				owner_username = user_res->second->name;

			page_out += "<p>Current owner: " + web::Escaping::HTMLEscape(owner_username) + " (user id: " + parcel->owner_id.toString() + ")</p>   \n";

			page_out += "<form action=\"/admin_set_parcel_owner_post\" method=\"post\">";
			page_out += "parcel id: <input type=\"number\" name=\"parcel_id\" value=\"" + parcel_id.toString() + "\"><br>";
			page_out += "new owner id: <input type=\"number\" name=\"new_owner_id\" value=\"" + parcel->owner_id.toString() + "\"><br>";
			page_out += "<input type=\"submit\" value=\"Change owner\">";
			page_out += "</form>";
		}
	} // End lock scope

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page_out);
}


void handleSetParcelOwnerPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		const int parcel_id    = request.getPostIntField("parcel_id");
		const int new_owner_id = request.getPostIntField("new_owner_id");

		{ // Lock scope

			WorldStateLock lock(world_state.mutex);
			const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			runtimeCheck(logged_in_user);

			// Lookup parcel
			const auto res = world_state.getRootWorldState()->parcels.find(ParcelID((uint32)parcel_id));
			if(res != world_state.getRootWorldState()->parcels.end())
			{
				// Found user for username
				Parcel* parcel = res->second.ptr();

				parcel->owner_id = UserID(new_owner_id);

				// Set parcel admins and writers to the new user as well.
				parcel->admin_ids  = std::vector<UserID>(1, UserID(new_owner_id));
				parcel->writer_ids = std::vector<UserID>(1, UserID(new_owner_id));
				world_state.getRootWorldState()->addParcelAsDBDirty(parcel, lock);

				world_state.denormaliseData(); // Update denormalised data which includes parcel owner name

				world_state.markAsChanged();
				world_state.addAdminAuditLogEntry(
					logged_in_user->id,
					logged_in_user->name,
					"Changed owner for parcel " + toString(parcel_id) + " to user " + toString(new_owner_id) + ".");

				web::ResponseUtils::writeRedirectTo(reply_info, "/parcel/" + toString(parcel_id));
			}
		} // End lock scope
	}
	catch(glare::Exception& e)
	{
		conPrint("handleSetParcelOwnerPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleMarkParcelAsNFTMintedPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		const int parcel_id = request.getPostIntField("parcel_id");

		{ // Lock scope

			Lock lock(world_state.mutex);

			// Lookup parcel
			const auto res = world_state.getRootWorldState()->parcels.find(ParcelID((uint32)parcel_id));
			if(res != world_state.getRootWorldState()->parcels.end())
			{
				Parcel* parcel = res->second.ptr();
				parcel->nft_status = Parcel::NFTStatus_MintedNFT;

				world_state.markAsChanged();

				web::ResponseUtils::writeRedirectTo(reply_info, "/parcel/" + toString(parcel_id));
			}
		} // End lock scope
	}
	catch(glare::Exception& e)
	{
		conPrint("handleMarkParcelAsNFTMintedPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleMarkParcelAsNotNFTPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		const int parcel_id = request.getPostIntField("parcel_id");

		{ // Lock scope

			WorldStateLock lock(world_state.mutex);

			// Lookup parcel
			const auto res = world_state.getRootWorldState()->parcels.find(ParcelID((uint32)parcel_id));
			if(res != world_state.getRootWorldState()->parcels.end())
			{
				Parcel* parcel = res->second.ptr();
				parcel->nft_status = Parcel::NFTStatus_NotNFT;
				world_state.getRootWorldState()->addParcelAsDBDirty(parcel, lock);

				world_state.markAsChanged();

				web::ResponseUtils::writeRedirectTo(reply_info, "/parcel/" + toString(parcel_id));
			}
		} // End lock scope
	}
	catch(glare::Exception& e)
	{
		conPrint("handleMarkParcelAsNotNFTPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


// Creates a new minting transaction for the parcel.
void handleRetryParcelMintPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		const int parcel_id = request.getPostIntField("parcel_id");

		{ // Lock scope
			WorldStateLock lock(world_state.mutex);

			User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);

			// Lookup parcel
			const auto res = world_state.getRootWorldState()->parcels.find(ParcelID((uint32)parcel_id));
			if(res != world_state.getRootWorldState()->parcels.end())
			{
				Parcel* parcel = res->second.ptr();

				// Find the last (greatest id) SubEthTransaction for this parcel.  We will use the assign-to eth address for this (Instead of admin eth address)
				SubEthTransaction* last_trans = NULL;
				for(auto it = world_state.sub_eth_transactions.begin(); it != world_state.sub_eth_transactions.end(); ++it)
				{
					SubEthTransaction* trans = it->second.ptr();
					if(trans->parcel_id == ParcelID((uint32)parcel_id))
						last_trans = trans;
				}

				if(last_trans == NULL)
					throw glare::Exception("no last transaction found.");


				// Make an Eth transaction to mint the parcel
				SubEthTransactionRef transaction = new SubEthTransaction();
				transaction->id = world_state.getNextSubEthTransactionUID();
				transaction->created_time = TimeStamp::currentTime();
				transaction->state = SubEthTransaction::State_New;
				transaction->initiating_user_id = logged_in_user->id;
				transaction->parcel_id = parcel->id;
				transaction->user_eth_address = last_trans->user_eth_address;
				world_state.addSubEthTransactionAsDBDirty(transaction);

				parcel->minting_transaction_id = transaction->id;
				
				world_state.getRootWorldState()->addParcelAsDBDirty(parcel, lock);

				world_state.sub_eth_transactions[transaction->id] = transaction;

				world_state.markAsChanged();

				web::ResponseUtils::writeRedirectTo(reply_info, "/admin_sub_eth_transactions");
			}
		} // End lock scope
	}
	catch(glare::Exception& e)
	{
		conPrint("handleRetryParcelMintPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleSetParcelVertexPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		const int parcel_id = request.getPostIntField("parcel_id");
		const int vert_i    = request.getPostIntField("vert_i");
		if(vert_i < 0 || vert_i >= 4)
			throw glare::Exception("invalid vert index");

		const std::string vert_string = stripHeadAndTailWhitespace(request.getPostField("vert_string").str());
		Parser parser(vert_string);
		Vec2d new_vert_pos;
		if(!parser.parseDouble(new_vert_pos.x))
			throw glare::Exception("failed to parse x coord");
		parser.parseWhiteSpace();
		if(!parser.parseDouble(new_vert_pos.y))
			throw glare::Exception("failed to parse y coord");

		{ // Lock scope
			WorldStateLock lock(world_state.mutex);

			// Lookup parcel
			const auto res = world_state.getRootWorldState()->parcels.find(ParcelID((uint32)parcel_id));
			if(res != world_state.getRootWorldState()->parcels.end())
			{
				Parcel* parcel = res->second.ptr();

				parcel->verts[vert_i] = new_vert_pos;
				parcel->build();
				
				world_state.getRootWorldState()->addParcelAsDBDirty(parcel, lock);
				world_state.markAsChanged();

				web::ResponseUtils::writeRedirectTo(reply_info, "/parcel/" + toString(parcel_id));
			}
		} // End lock scope
	}
	catch(glare::Exception& e)
	{
		conPrint("handleSetParcelVertexPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleSetParcelZBoundsPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		const int parcel_id = request.getPostIntField("parcel_id");
		
		const std::string zbounds_string = stripHeadAndTailWhitespace(request.getPostField("zbounds_string").str());
		Parser parser(zbounds_string);
		Vec2d new_zbounds;
		if(!parser.parseDouble(new_zbounds.x))
			throw glare::Exception("failed to parse x coord");
		parser.parseWhiteSpace();
		if(!parser.parseDouble(new_zbounds.y))
			throw glare::Exception("failed to parse y coord");

		{ // Lock scope
			WorldStateLock lock(world_state.mutex);

			// Lookup parcel
			const auto res = world_state.getRootWorldState()->parcels.find(ParcelID((uint32)parcel_id));
			if(res != world_state.getRootWorldState()->parcels.end())
			{
				Parcel* parcel = res->second.ptr();

				parcel->zbounds = new_zbounds;
				parcel->build();
				
				world_state.getRootWorldState()->addParcelAsDBDirty(parcel, lock);
				world_state.markAsChanged();

				web::ResponseUtils::writeRedirectTo(reply_info, "/parcel/" + toString(parcel_id));
			}
		} // End lock scope
	}
	catch(glare::Exception& e)
	{
		conPrint("handleSetParcelZBoundsPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleSetParcelWidthsPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		const int parcel_id = request.getPostIntField("parcel_id");
		
		const std::string widths_string = stripHeadAndTailWhitespace(request.getPostField("widths_string").str());
		Parser parser(widths_string);
		Vec2d widths;
		if(!parser.parseDouble(widths.x))
			throw glare::Exception("failed to parse x coord");
		parser.parseWhiteSpace();
		if(!parser.parseDouble(widths.y))
			throw glare::Exception("failed to parse y coord");

		{ // Lock scope
			WorldStateLock lock(world_state.mutex);

			// Lookup parcel
			const auto res = world_state.getRootWorldState()->parcels.find(ParcelID((uint32)parcel_id));
			if(res != world_state.getRootWorldState()->parcels.end())
			{
				Parcel* parcel = res->second.ptr();

				parcel->verts[1] = parcel->verts[0] + Vec2d(widths.x, 0);
				parcel->verts[2] = parcel->verts[0] + widths;
				parcel->verts[3] = parcel->verts[0] + Vec2d(0, widths.y);
				parcel->build();

				world_state.getRootWorldState()->addParcelAsDBDirty(parcel, lock);
				world_state.markAsChanged();

				web::ResponseUtils::writeRedirectTo(reply_info, "/parcel/" + toString(parcel_id));
			}
		} // End lock scope
	}
	catch(glare::Exception& e)
	{
		conPrint("handleSetParcelWidthsPost error: " + e.what());
	}
}


// Sets the state of a minting transaction to new.
void handleSetTransactionStateToNewPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		const int transaction_id = request.getPostIntField("transaction_id");

		{ // Lock scope
			Lock lock(world_state.mutex);

			// Lookup transaction
			const auto res = world_state.sub_eth_transactions.find(transaction_id);
			if(res != world_state.sub_eth_transactions.end())
			{
				SubEthTransaction* transaction = res->second.ptr();
				transaction->state = SubEthTransaction::State_New;
				
				world_state.addSubEthTransactionAsDBDirty(transaction);

				web::ResponseUtils::writeRedirectTo(reply_info, "/admin_sub_eth_transaction/" + toString(transaction_id));
			}
		} // End lock scope
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleSetTransactionStateToNewPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


// Sets the state of a minting transaction to completed.
void handleSetTransactionStateToCompletedPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		const int transaction_id = request.getPostIntField("transaction_id");

		{ // Lock scope
			Lock lock(world_state.mutex);

			// Lookup transaction
			const auto res = world_state.sub_eth_transactions.find(transaction_id);
			if(res != world_state.sub_eth_transactions.end())
			{
				SubEthTransaction* transaction = res->second.ptr();
				transaction->state = SubEthTransaction::State_Completed;
				
				world_state.addSubEthTransactionAsDBDirty(transaction);

				web::ResponseUtils::writeRedirectTo(reply_info, "/admin_sub_eth_transaction/" + toString(transaction_id));
			}
		} // End lock scope
	}
	catch(glare::Exception& e)
	{
		conPrint("handleSetTransactionStateToCompletedPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleSetTransactionHashPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		const int transaction_id = request.getPostIntField("transaction_id");
		const web::UnsafeString hash_str = request.getPostField("hash");

		const UInt256 hash = UInt256::parseFromHexString(hash_str.str());

		{ // Lock scope
			Lock lock(world_state.mutex);

			// Lookup transaction
			const auto res = world_state.sub_eth_transactions.find(transaction_id);
			if(res != world_state.sub_eth_transactions.end())
			{
				SubEthTransaction* transaction = res->second.ptr();

				transaction->transaction_hash = hash;
				
				world_state.addSubEthTransactionAsDBDirty(transaction);

				web::ResponseUtils::writeRedirectTo(reply_info, "/admin_sub_eth_transaction/" + toString(transaction_id));
			}
		} // End lock scope
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleSetTransactionHashPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleSetTransactionNoncePost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		const int transaction_id = request.getPostIntField("transaction_id");
		const int nonce = request.getPostIntField("nonce");

		{ // Lock scope
			Lock lock(world_state.mutex);

			// Lookup transaction
			const auto res = world_state.sub_eth_transactions.find(transaction_id);
			if(res != world_state.sub_eth_transactions.end())
			{
				SubEthTransaction* transaction = res->second.ptr();

				transaction->nonce = (uint64)nonce;
				
				world_state.addSubEthTransactionAsDBDirty(transaction);

				web::ResponseUtils::writeRedirectTo(reply_info, "/admin_sub_eth_transaction/" + toString(transaction_id));
			}
		} // End lock scope
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleSetTransactionNoncePost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


// Deletes a transaction
void handleDeleteTransactionPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		const int transaction_id = request.getPostIntField("transaction_id");

		{ // Lock scope
			Lock lock(world_state.mutex);

			// Lookup transaction
			auto res = world_state.sub_eth_transactions.find(transaction_id);
			if(res != world_state.sub_eth_transactions.end())
			{
				SubEthTransaction* trans = res->second.ptr();

				world_state.db_dirty_sub_eth_transactions.erase(trans); // Remove from dirty set so it doesn't get written to the DB

				world_state.db_records_to_delete.insert(trans->database_key);

				world_state.sub_eth_transactions.erase(transaction_id);

				world_state.markAsChanged();
			}
		} // End lock scope

		web::ResponseUtils::writeRedirectTo(reply_info, "/admin_sub_eth_transactions");
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleDeleteTransactionPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleCreateParcelPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		ParcelID new_id;
		std::string created_world_name;
		std::string owner_username;
		std::vector<UserID> writer_ids;

		{ // Lock scope

			WorldStateLock lock(world_state.mutex);

			User* user = LoginHandlers::getLoggedInUser(world_state, request);
			runtimeCheck(user);
			const std::string world_name_field = request.getPostField("world_name").str();
			owner_username = stripHeadAndTailWhitespace(request.getPostField("owner_username").str());
			const std::string writer_usernames_csv = request.getPostField("writer_usernames").str();
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

			created_world_name = normaliseWorldNameField(world_name_field);
			const auto world_res = world_state.world_states.find(created_world_name);
			if(world_res == world_state.world_states.end())
				throw glare::Exception("Could not find world '" + created_world_name + "'.");
			ServerWorldState* target_world = world_res->second.ptr();

			UserID owner_id = user->id;
			if(!owner_username.empty())
			{
				if(!getUserIDForUsername(world_state, owner_username, owner_id))
					throw glare::Exception("Could not find owner user '" + owner_username + "'.");
			}
			else
			{
				owner_username = user->name;
			}

			std::string writer_parse_error;
			if(!parseCommaSeparatedUsernamesToIDs(world_state, writer_usernames_csv, writer_ids, writer_parse_error))
				throw glare::Exception(writer_parse_error);

			if(!ContainerUtils::contains(writer_ids, owner_id))
				writer_ids.push_back(owner_id);

			// Find max parcel id in target world.
			uint32 max_id = 0;
			for(auto it = target_world->getParcels(lock).begin(); it != target_world->getParcels(lock).end(); ++it)
			{
				if(it->second->id.valid())
					max_id = myMax(max_id, it->second->id.value());
			}

			new_id = ParcelID(max_id + 1);

			Parcel* parcel = new Parcel();
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

			target_world->getParcels(lock)[new_id] = parcel;
			target_world->addParcelAsDBDirty(parcel, lock);
			world_state.markAsChanged();
			const std::string world_display_name = created_world_name.empty() ? std::string("[root world]") : created_world_name;
			world_state.addAdminAuditLogEntry(
				user->id,
				user->name,
				"Created parcel " + new_id.toString() + " in world '" + world_display_name + "', owner='" + owner_username + "', editors_count=" + toString(writer_ids.size()) + ".");

			world_state.setUserWebMessage(user->id, "Parcel " + new_id.toString() + " created in world '" + world_display_name + "' and assigned to '" + owner_username + "'.");
		} // End lock scope

		if(created_world_name.empty())
			web::ResponseUtils::writeRedirectTo(reply_info, "/parcel/" + toString(new_id.value()));
		else
			web::ResponseUtils::writeRedirectTo(reply_info, "/world/" + WorldHandlers::URLEscapeWorldName(created_world_name));
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleCreateParcelPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleDeleteParcelPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		const ParcelID parcel_id = ParcelID(request.getPostIntField("parcel_id"));
		if(!parcel_id.valid())
			throw glare::Exception("Invalid parcel_id.");

		{ // Lock scope
			WorldStateLock lock(world_state.mutex);

			User* user = LoginHandlers::getLoggedInUser(world_state, request);
			runtimeCheck(user);

			Reference<ServerWorldState> root_world = world_state.getRootWorldState();

			const auto parcel_res = root_world->parcels.find(parcel_id);
			if(parcel_res == root_world->parcels.end())
				throw glare::Exception("Parcel not found.");

			ParcelRef parcel = parcel_res->second;
			if(!parcel->parcel_auction_ids.empty())
				throw glare::Exception("Parcel has auction records, delete/terminate auctions first.");

			// Remove from dirty set first so it won't be re-written to DB after delete.
			root_world->getDBDirtyParcels(lock).erase(parcel);

			if(parcel->database_key.valid())
				world_state.db_records_to_delete.insert(parcel->database_key);

			root_world->parcels.erase(parcel_res);

			world_state.markAsChanged();
			world_state.setUserWebMessage(user->id, "Parcel deleted.");
			world_state.addAdminAuditLogEntry(user->id, user->name, "Deleted parcel " + parcel_id.toString() + ".");
		} // End lock scope

		web::ResponseUtils::writeRedirectTo(reply_info, "/admin_parcels");
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleDeleteParcelPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


static bool deleteChatBotByID(ServerAllWorldsState& world_state, const uint64 chatbot_id, const std::string& reason, WorldStateLock& lock)
{
	for(auto world_it = world_state.world_states.begin(); world_it != world_state.world_states.end(); ++world_it)
	{
		ServerWorldState* world = world_it->second.ptr();
		auto chatbot_it = world->getChatBots(lock).find(chatbot_id);
		if(chatbot_it != world->getChatBots(lock).end())
		{
			ChatBotRef chatbot = chatbot_it->second;
			if(chatbot->avatar)
			{
				chatbot->avatar->state = Avatar::State_Dead;
				chatbot->avatar->other_dirty = true;
			}

			world->getDBDirtyChatBots(lock).erase(chatbot);
			if(chatbot->database_key.valid())
				world_state.db_records_to_delete.insert(chatbot->database_key);

			world->getChatBots(lock).erase(chatbot_it);
			world_state.markAsChanged();
			(void)reason;
			return true;
		}
	}

	return false;
}


void handleSetChatBotDisabledPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		const uint64 chatbot_id = (uint64)request.getPostIntField("chatbot_id");
		const bool disable_chatbot = request.getPostIntField("new_value") != 0;

		{ // lock scope
			WorldStateLock lock(world_state.mutex);

			const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			runtimeCheck(logged_in_user);

			bool found = false;
			for(auto world_it = world_state.world_states.begin(); world_it != world_state.world_states.end(); ++world_it)
			{
				ServerWorldState* world = world_it->second.ptr();
				auto chatbot_it = world->getChatBots(lock).find(chatbot_id);
				if(chatbot_it != world->getChatBots(lock).end())
				{
					ChatBot* chatbot = chatbot_it->second.ptr();
					BitUtils::setOrZeroBit(chatbot->flags, ChatBot::DISABLED_FLAG, disable_chatbot);
					world->addChatBotAsDBDirty(chatbot, lock);
					world_state.markAsChanged();
					world_state.setUserWebMessage(logged_in_user->id, std::string(disable_chatbot ? "Disabled" : "Enabled") + " chatbot " + toString(chatbot_id) + ".");
					world_state.addAdminAuditLogEntry(logged_in_user->id, logged_in_user->name, std::string(disable_chatbot ? "Disabled" : "Enabled") + " chatbot " + toString(chatbot_id) + ".");
					found = true;
					break;
				}
			}

			if(!found)
				world_state.setUserWebMessage(logged_in_user->id, "Chatbot not found.");
		}

		web::ResponseUtils::writeRedirectTo(reply_info, "/admin_chatbots");
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleSetChatBotDisabledPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleSetChatBotsDisabledPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		const std::string world_name_filter = stripHeadAndTailWhitespace(request.getPostField("world_name").str());
		const std::string username_filter = stripHeadAndTailWhitespace(request.getPostField("username").str());
		const bool disable_chatbots = request.getPostIntField("new_value") != 0;

		{ // lock scope
			WorldStateLock lock(world_state.mutex);

			const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			runtimeCheck(logged_in_user);

			int num_updated = 0;
			for(auto world_it = world_state.world_states.begin(); world_it != world_state.world_states.end(); ++world_it)
			{
				ServerWorldState* world = world_it->second.ptr();

				for(auto chatbot_it = world->getChatBots(lock).begin(); chatbot_it != world->getChatBots(lock).end(); ++chatbot_it)
				{
					ChatBot* chatbot = chatbot_it->second.ptr();
					const std::string owner_name = getUserNameForID(world_state, chatbot->owner_id);

					if(!chatbotMatchesFilters(world_name_filter, username_filter, world_it->first, owner_name))
						continue;

					BitUtils::setOrZeroBit(chatbot->flags, ChatBot::DISABLED_FLAG, disable_chatbots);
					world->addChatBotAsDBDirty(chatbot, lock);
					num_updated++;
				}
			}

			world_state.markAsChanged();
			world_state.setUserWebMessage(logged_in_user->id, std::string(disable_chatbots ? "Disabled " : "Enabled ") + toString(num_updated) + " chatbot(s).");
			world_state.addAdminAuditLogEntry(
				logged_in_user->id,
				logged_in_user->name,
				std::string(disable_chatbots ? "Disabled " : "Enabled ") + toString(num_updated) + " chatbot(s), world_filter='" + world_name_filter + "', username_filter='" + username_filter + "'.");
		}

		web::ResponseUtils::writeRedirectTo(reply_info, "/admin_chatbots?world_name=" + web::Escaping::URLEscape(world_name_filter) + "&username=" + web::Escaping::URLEscape(username_filter));
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleSetChatBotsDisabledPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleDeleteChatBotPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		const uint64 chatbot_id = (uint64)request.getPostIntField("chatbot_id");

		{ // lock scope
			WorldStateLock lock(world_state.mutex);

			const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			runtimeCheck(logged_in_user);

			if(deleteChatBotByID(world_state, chatbot_id, "admin_delete", lock))
			{
				world_state.setUserWebMessage(logged_in_user->id, "Deleted chatbot.");
				world_state.addAdminAuditLogEntry(logged_in_user->id, logged_in_user->name, "Deleted chatbot " + toString(chatbot_id) + ".");
			}
			else
			{
				world_state.setUserWebMessage(logged_in_user->id, "Chatbot not found.");
			}
		}

		web::ResponseUtils::writeRedirectTo(reply_info, "/admin_chatbots");
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleDeleteChatBotPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleDeleteChatBotsPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		const std::string world_name_filter = stripHeadAndTailWhitespace(request.getPostField("world_name").str());
		const std::string username_filter = stripHeadAndTailWhitespace(request.getPostField("username").str());

		{ // lock scope
			WorldStateLock lock(world_state.mutex);

			const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			runtimeCheck(logged_in_user);

			int num_deleted = 0;
			for(auto world_it = world_state.world_states.begin(); world_it != world_state.world_states.end(); ++world_it)
			{
				ServerWorldState* world = world_it->second.ptr();
				ServerWorldState::ChatBotMapType& chatbots = world->getChatBots(lock);

				for(auto chatbot_it = chatbots.begin(); chatbot_it != chatbots.end(); )
				{
					ChatBotRef chatbot = chatbot_it->second;
					const std::string owner_name = getUserNameForID(world_state, chatbot->owner_id);

					if(!chatbotMatchesFilters(world_name_filter, username_filter, world_it->first, owner_name))
					{
						++chatbot_it;
						continue;
					}

					if(chatbot->avatar)
					{
						chatbot->avatar->state = Avatar::State_Dead;
						chatbot->avatar->other_dirty = true;
					}

					world->getDBDirtyChatBots(lock).erase(chatbot);
					if(chatbot->database_key.valid())
						world_state.db_records_to_delete.insert(chatbot->database_key);

					chatbot_it = chatbots.erase(chatbot_it);
					num_deleted++;
				}
			}

			world_state.markAsChanged();
			world_state.setUserWebMessage(logged_in_user->id, "Deleted " + toString(num_deleted) + " chatbot(s).");
			world_state.addAdminAuditLogEntry(
				logged_in_user->id,
				logged_in_user->name,
				"Deleted " + toString(num_deleted) + " chatbot(s), world_filter='" + world_name_filter + "', username_filter='" + username_filter + "'.");
		}

		web::ResponseUtils::writeRedirectTo(reply_info, "/admin_chatbots?world_name=" + web::Escaping::URLEscape(world_name_filter) + "&username=" + web::Escaping::URLEscape(username_filter));
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleDeleteChatBotsPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleRegenerateParcelAuctionScreenshots(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		const int parcel_auction_id = request.getPostIntField("parcel_auction_id");

		{ // Lock scope

			Lock lock(world_state.mutex);

			// Lookup parcel auction
			const auto res = world_state.parcel_auctions.find(parcel_auction_id);
			if(res != world_state.parcel_auctions.end())
			{
				ParcelAuction* auction = res->second.ptr();

				// Lookup parcel
				const auto res2 = world_state.getRootWorldState()->parcels.find(auction->parcel_id);
				if(res2 != world_state.getRootWorldState()->parcels.end())
				{
					const Parcel* parcel = res2->second.ptr();

					for(size_t z=0; z<auction->screenshot_ids.size(); ++z)
					{
						const uint64 screenshot_id = auction->screenshot_ids[z];

						auto shot_res = world_state.screenshots.find(screenshot_id);
						if(shot_res != world_state.screenshots.end())
						{
							Screenshot* shot = shot_res->second.ptr();

							// Update pos and angles
							if(z == 0) // If this is the first, close-in shot.  NOTE: bit of a hack
							{
								parcel->getScreenShotPosAndAngles(shot->cam_pos, shot->cam_angles);
							}
							else
							{
								parcel->getFarScreenShotPosAndAngles(shot->cam_pos, shot->cam_angles);
							}

							shot->state = Screenshot::ScreenshotState_notdone;
							world_state.addScreenshotAsDBDirty(shot);
						}
					}
				}
			}
		} // End lock scope

		web::ResponseUtils::writeRedirectTo(reply_info, "/parcel_auction/" + toString(parcel_auction_id));
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleRegenerateParcelAuctionScreenshots error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


// See also ParcelHandlers::handleRegenerateParcelScreenshots()
void handleRegenerateParcelScreenshots(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		const ParcelID parcel_id = ParcelID(request.getPostIntField("parcel_id"));

		{ // Lock scope

			Lock lock(world_state.mutex);

			// Lookup parcel
			const auto res = world_state.getRootWorldState()->parcels.find(parcel_id);
			if(res != world_state.getRootWorldState()->parcels.end())
			{
				Parcel* parcel = res->second.ptr();

				for(size_t z=0; z<parcel->screenshot_ids.size(); ++z)
				{
					const uint64 screenshot_id = parcel->screenshot_ids[z];

					auto shot_res = world_state.screenshots.find(screenshot_id);
					if(shot_res != world_state.screenshots.end())
					{
						Screenshot* shot = shot_res->second.ptr();

						// Update pos and angles
						if(z == 0) // If this is the first, close-in shot.  NOTE: bit of a hack
						{
							parcel->getScreenShotPosAndAngles(shot->cam_pos, shot->cam_angles);
						}
						else
						{
							parcel->getFarScreenShotPosAndAngles(shot->cam_pos, shot->cam_angles);
						}

						shot->is_map_tile = false; // There was a bug where some screenshots got mixed up with map tile screenshots, make sure the screenshot is not marked as a map tile.
						shot->width_px = 650;
						shot->highlight_parcel_id = (int)parcel->id.value();

						shot->state = Screenshot::ScreenshotState_notdone;
						world_state.addScreenshotAsDBDirty(shot);
					}
				}
			}
		} // End lock scope

		web::ResponseUtils::writeRedirectTo(reply_info, "/parcel/" + parcel_id.toString());
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleRegenerateParcelScreenshots error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleRegenerateMultipleParcelScreenshots(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		const int start_parcel_id	= request.getPostIntField("start_parcel_id");
		const int end_parcel_id		= request.getPostIntField("end_parcel_id");

		{ // Lock scope

			WorldStateLock lock(world_state.mutex);

			for(auto it = world_state.getRootWorldState()->parcels.begin(); it != world_state.getRootWorldState()->parcels.end(); ++it)
			{
				Parcel* parcel = it->second.ptr();

				if((int)parcel->id.value() >= start_parcel_id && (int)parcel->id.value() <= end_parcel_id)
				{
					if(parcel->screenshot_ids.empty()) // If parcel does not have any screenshots yet:
					{
						uint64 next_shot_id = world_state.getNextScreenshotUID();

						// Create close-in screenshot
						{
							ScreenshotRef shot = new Screenshot();
							shot->id = next_shot_id++;
							parcel->getScreenShotPosAndAngles(shot->cam_pos, shot->cam_angles);
							shot->width_px = 650;
							shot->highlight_parcel_id = (int)parcel->id.value();
							shot->created_time = TimeStamp::currentTime();
							shot->state = Screenshot::ScreenshotState_notdone;

							world_state.screenshots[shot->id] = shot;

							parcel->screenshot_ids.push_back(shot->id);

							world_state.addScreenshotAsDBDirty(shot);
						}
						// Zoomed-out screenshot
						{
							ScreenshotRef shot = new Screenshot();
							shot->id = next_shot_id++;
							parcel->getFarScreenShotPosAndAngles(shot->cam_pos, shot->cam_angles);
							shot->width_px = 650;
							shot->highlight_parcel_id = (int)parcel->id.value();
							shot->created_time = TimeStamp::currentTime();
							shot->state = Screenshot::ScreenshotState_notdone;

							world_state.screenshots[shot->id] = shot;

							parcel->screenshot_ids.push_back(shot->id);

							world_state.addScreenshotAsDBDirty(shot);
						}

						world_state.getRootWorldState()->addParcelAsDBDirty(parcel, lock);
						world_state.markAsChanged();

						conPrint("Created screenshots for parcel " + parcel->id.toString());
					}
					else
					{
						for(size_t z=0; z<parcel->screenshot_ids.size(); ++z)
						{
							const uint64 screenshot_id = parcel->screenshot_ids[z];

							auto shot_res = world_state.screenshots.find(screenshot_id);
							if(shot_res != world_state.screenshots.end())
							{
								Screenshot* shot = shot_res->second.ptr();

								// Update pos and angles
								if(z == 0) // If this is the first, close-in shot.  NOTE: bit of a hack
								{
									parcel->getScreenShotPosAndAngles(shot->cam_pos, shot->cam_angles);
								}
								else
								{
									parcel->getFarScreenShotPosAndAngles(shot->cam_pos, shot->cam_angles);
								}

								shot->is_map_tile = false; // There was a bug where some screenshots got mixed up with map tile screenshots, make sure the screenshot is not marked as a map tile.
								shot->width_px = 650;
								shot->highlight_parcel_id = (int)parcel->id.value();

								shot->state = Screenshot::ScreenshotState_notdone;
								world_state.addScreenshotAsDBDirty(shot);
							}

							conPrint("Regenerated screenshots for parcel " + parcel->id.toString());
						}
					}
				}
			}
		} // End lock scope

		web::ResponseUtils::writeRedirectTo(reply_info, "/admin_parcels");
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleRegenerateMultipleParcelScreenshots error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleTerminateParcelAuction(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		const int parcel_auction_id = request.getPostIntField("parcel_auction_id");

		{ // Lock scope

			Lock lock(world_state.mutex);

			// Lookup parcel auction
			const auto res = world_state.parcel_auctions.find(parcel_auction_id);
			if(res != world_state.parcel_auctions.end())
			{
				ParcelAuction* auction = res->second.ptr();

				auction->auction_end_time = TimeStamp::currentTime(); // Just mark the end time as now
				
				world_state.addParcelAuctionAsDBDirty(auction);
			}
		} // End lock scope

		web::ResponseUtils::writeRedirectTo(reply_info, "/parcel_auction/" + toString(parcel_auction_id));
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleTerminateParcelAuction error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleRegenMapTilesPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		{ // Lock scope

			Lock lock(world_state.mutex);

			// Mark all tile sceenshots as not done.
			for(auto it = world_state.map_tile_info.info.begin(); it != world_state.map_tile_info.info.end(); ++it)
			{
				TileInfo& tile_info = it->second;
				if(tile_info.cur_tile_screenshot.nonNull())
				{
					tile_info.cur_tile_screenshot->state = Screenshot::ScreenshotState_notdone;
				}
			}

			world_state.map_tile_info.db_dirty = true;
			world_state.markAsChanged();
			//world_state.setUserWebMessage("Regenerating map tiles.");

		} // End lock scope

		web::ResponseUtils::writeRedirectTo(reply_info, "/admin_map");
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleRegenMapTilesPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


// Instead of marking screenshots as not done, creates new Sceenshot objects.  Used for the database sceenshot ID clash issue.
void handleRecreateMapTilesPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		{ // Lock scope

			Lock lock(world_state.mutex);

			uint64 next_shot_id = world_state.getNextScreenshotUID();

			for(auto it = world_state.map_tile_info.info.begin(); it != world_state.map_tile_info.info.end(); ++it)
			{
				const Vec3<int> key = it->first;

				TileInfo& tile_info = it->second;

				tile_info.cur_tile_screenshot = new Screenshot();
				tile_info.cur_tile_screenshot->id = next_shot_id++;
				tile_info.cur_tile_screenshot->created_time = TimeStamp::currentTime();
				tile_info.cur_tile_screenshot->state = Screenshot::ScreenshotState_notdone;
				tile_info.cur_tile_screenshot->is_map_tile = true;
				tile_info.cur_tile_screenshot->tile_x = key.x;
				tile_info.cur_tile_screenshot->tile_y = key.y;
				tile_info.cur_tile_screenshot->tile_z = key.z;
			}

			world_state.map_tile_info.db_dirty = true;
			world_state.markAsChanged();
			//world_state.setUserWebMessage("Regenerating map tiles.");

		} // End lock scope

		web::ResponseUtils::writeRedirectTo(reply_info, "/admin_map");
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleRecreateMapTilesPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleSetMinNextNoncePost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		{ // Lock scope

			Lock lock(world_state.mutex);

			world_state.eth_info.min_next_nonce = request.getPostIntField("min_next_nonce");
			world_state.eth_info.db_dirty = true;
			
			world_state.markAsChanged();

		} // End lock scope

		web::ResponseUtils::writeRedirectTo(reply_info, "/admin_sub_eth_transactions");
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleSetMinNextNoncePost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleSetServerAdminMessagePost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		{ // Lock scope

			Lock lock(world_state.mutex);

			world_state.server_admin_message = request.getPostField("msg").str();
			world_state.server_admin_message_changed = true;

		} // End lock scope

		//world_state.setUserWebMessage("Set server admin message.");

		web::ResponseUtils::writeRedirectTo(reply_info, "/admin");
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleSetServerAdminMessagePost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleSetReadOnlyModePost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		{ // Lock scope

			Lock lock(world_state.mutex);

			world_state.read_only_mode = request.getPostIntField("read_only_mode") != 0;

		} // End lock scope

		//world_state.setUserWebMessage("Set server admin message.");

		web::ResponseUtils::writeRedirectTo(reply_info, "/admin");
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleSetReadOnlyModePost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleSetFeatureFlagPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		{ // Lock scope

			Lock lock(world_state.mutex);

			const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			runtimeCheck(logged_in_user);

			const int flag_bit_index = request.getPostIntField("flag_bit_index");
			const int new_value  = request.getPostIntField("new_value");

			if(flag_bit_index >= 0 && flag_bit_index < 64)
			{
				const uint64 bitflag = 1ull << flag_bit_index;
				BitUtils::setOrZeroBit(world_state.feature_flag_info.feature_flags, bitflag, /*should set=*/new_value != 0);
				world_state.addAdminAuditLogEntry(
					logged_in_user->id,
					logged_in_user->name,
					"Set feature flag " + featureFlagNameForBit(bitflag) + " (" + toString(bitflag) + ") to " + toString(new_value != 0 ? 1 : 0) + ".");
			}

			world_state.feature_flag_info.db_dirty = true;
			world_state.markAsChanged();

		} // End lock scope

		web::ResponseUtils::writeRedirectTo(reply_info, "/admin");
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleSetFeatureFlagPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleReloadWebDataPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		UserID user_id;
		std::string username;
		{
			Lock lock(world_state.mutex);
			const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			runtimeCheck(logged_in_user);
			user_id = logged_in_user->id;
			username = logged_in_user->name;
		}

		if(world_state.web_data_store)
			world_state.web_data_store->loadAndCompressFiles();

		{
			Lock lock(world_state.mutex);
			world_state.setUserWebMessage(user_id, "Reloaded web data from disk.");
			world_state.addAdminAuditLogEntry(user_id, username, "Reloaded web data from disk.");
		}

		web::ResponseUtils::writeRedirectTo(reply_info, "/admin");
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleReloadWebDataPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleForceDynTexUpdatePost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		{ // Lock scope
			Lock lock(world_state.mutex);
			world_state.force_dyn_tex_update = true;
		} // End lock scope

		web::ResponseUtils::writeRedirectTo(reply_info, "/admin");
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleForceDynTexUpdatePost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleSetUserAsWorldGardenerPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		const int user_id = request.getPostIntField("user_id");
		const int gardener = request.getPostIntField("gardener");

		{ // Lock scope

			Lock lock(world_state.mutex);

			// Lookup user
			const auto res = world_state.user_id_to_users.find(UserID(user_id));
			if(res != world_state.user_id_to_users.end())
			{
				User* user = res->second.ptr();

				BitUtils::setOrZeroBit(user->flags, User::WORLD_GARDENER_FLAG, gardener != 0);

				world_state.addUserAsDBDirty(user);
			}
		} // End lock scope

		web::ResponseUtils::writeRedirectTo(reply_info, "/admin_user/" + toString(user_id));
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleSetUserAsWorldGardenerPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleSetUserAllowDynTexUpdatePost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		const int user_id = request.getPostIntField("user_id");
		const int allow = request.getPostIntField("allow");

		{ // Lock scope

			Lock lock(world_state.mutex);

			// Lookup user
			const auto res = world_state.user_id_to_users.find(UserID(user_id));
			if(res != world_state.user_id_to_users.end())
			{
				User* user = res->second.ptr();

				BitUtils::setOrZeroBit(user->flags, User::ALLOW_DYN_TEX_UPDATE_CHECKING, allow != 0);

				world_state.addUserAsDBDirty(user);
			}
		} // End lock scope

		web::ResponseUtils::writeRedirectTo(reply_info, "/admin_user/" + toString(user_id));
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleSetUserAllowDynTexUpdatePost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleResetUserPasswordPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, password changes are currently disabled.");

		const int user_id = request.getPostIntField("user_id");
		const bool generate_password = (request.getPostField("generate") == "1");
		std::string new_password = stripHeadAndTailWhitespace(request.getPostField("new_password").str());

		{ // Lock scope
			WorldStateLock lock(world_state.mutex);

			User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			runtimeCheck(logged_in_user);

			const auto user_res = world_state.user_id_to_users.find(UserID(user_id));
			if(user_res == world_state.user_id_to_users.end())
				throw glare::Exception("Could not find user.");

			User* target_user = user_res->second.ptr();

			if(new_password.empty())
			{
				if(!generate_password)
					throw glare::Exception("Manual password is empty and generation is disabled.");

				new_password = generateReadablePassword();
			}

			if(new_password.size() < 6)
				throw glare::Exception("New password is too short, must have at least 6 characters.");

			target_user->setNewPasswordAndSalt(new_password);
			target_user->password_resets.clear();
			world_state.addUserAsDBDirty(target_user);
			world_state.markAsChanged();

			world_state.addAdminAuditLogEntry(
				logged_in_user->id,
				logged_in_user->name,
				"Reset password for user " + toString(user_id) + " (" + target_user->name + ").");

			world_state.setUserWebMessage(logged_in_user->id, "New password for user '" + target_user->name + "': " + new_password);
		} // End lock scope

		web::ResponseUtils::writeRedirectTo(reply_info, "/admin_user/" + toString(user_id));
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleResetUserPasswordPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleNewNewsPostPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		NewsPostRef news_post = new NewsPost();

		{ // Lock scope

			Lock lock(world_state.mutex);

			User* user = LoginHandlers::getLoggedInUser(world_state, request);
			runtimeCheck(user != NULL);

			news_post->id = world_state.getNextNewsPostUID();
			news_post->creator_id = user->id;
			news_post->created_time = TimeStamp::currentTime();
			news_post->last_modified_time = TimeStamp::currentTime();

			world_state.news_posts.insert(std::make_pair(news_post->id, news_post));

			world_state.addNewsPostAsDBDirty(news_post);

			world_state.markAsChanged();
		} // End lock scope

		web::ResponseUtils::writeRedirectTo(reply_info, "/news_post/" + toString(news_post->id));
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleNewNewsPostPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleRebuildWorldLODChunks(ServerAllWorldsState& all_worlds_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(all_worlds_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		const web::UnsafeString world_name = request.getPostField("world_name");

		{ // Lock scope

			WorldStateLock lock(all_worlds_state.mutex);

			if(world_name == "ALL")
			{
				// Mark all chunks in all worlds as dirty
				for(auto it = all_worlds_state.world_states.begin(); it != all_worlds_state.world_states.end(); ++it)
				{
					ServerWorldState* world_state = it->second.ptr();

					ServerWorldState::LODChunkMapType& lod_chunks = world_state->getLODChunks(lock);
					for(auto lod_it = lod_chunks.begin(); lod_it != lod_chunks.end(); ++lod_it)
					{
						LODChunk* chunk = lod_it->second.ptr();
						if(!chunk->needs_rebuild)
						{
							chunk->needs_rebuild = true;

							world_state->addLODChunkAsDBDirty(chunk, lock);
							all_worlds_state.markAsChanged();
						}
					}
				}
			}
			else
			{
				auto res = all_worlds_state.world_states.find(world_name.str());

				if(res != all_worlds_state.world_states.end())
				{
					ServerWorldState* world_state = res->second.ptr();
					ServerWorldState::LODChunkMapType& lod_chunks = world_state->getLODChunks(lock);
					for(auto lod_it = lod_chunks.begin(); lod_it != lod_chunks.end(); ++lod_it)
					{
						LODChunk* chunk = lod_it->second.ptr();
						if(!chunk->needs_rebuild)
						{
							chunk->needs_rebuild = true;

							world_state->addLODChunkAsDBDirty(chunk, lock);
							all_worlds_state.markAsChanged();
						}
					}
				}
			}
		} // End lock scope

		web::ResponseUtils::writeRedirectTo(reply_info, "/admin_lod_chunks");
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleRebuildWorldLODChunks error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


} // end namespace AdminHandlers
