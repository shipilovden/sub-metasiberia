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
#include "../shared/GestureSettings.h"
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
#include <cctype>
#include <limits>


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


static bool isAllDigitsString(const std::string& s)
{
	if(s.empty())
		return false;
	for(size_t i=0; i<s.size(); ++i)
		if(!::isdigit((unsigned char)s[i]))
			return false;
	return true;
}


static bool resolveUserIDFromRef(ServerAllWorldsState& world_state, const std::string& user_ref, UserID& user_id_out, std::string& error_out)
{
	const std::string trimmed = stripHeadAndTailWhitespace(user_ref);
	if(trimmed.empty())
	{
		error_out = "User reference is empty.";
		return false;
	}

	if(isAllDigitsString(trimmed))
	{
		const uint64 user_id_u64 = stringToUInt64(trimmed);
		const UserID candidate((uint32)user_id_u64);
		const auto user_res = world_state.user_id_to_users.find(candidate);
		if(user_res == world_state.user_id_to_users.end())
		{
			error_out = "Could not find user with id '" + trimmed + "'.";
			return false;
		}
		user_id_out = candidate;
		return true;
	}

	if(!getUserIDForUsername(world_state, trimmed, user_id_out))
	{
		error_out = "Could not find user '" + trimmed + "'.";
		return false;
	}

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


static bool parseCommaSeparatedUserRefsToIDs(ServerAllWorldsState& world_state, const std::string& user_refs_csv, std::vector<UserID>& user_ids_out, std::string& error_out)
{
	const std::vector<std::string> user_ref_fields = ::split(user_refs_csv, ',');
	for(size_t i=0; i<user_ref_fields.size(); ++i)
	{
		const std::string user_ref = stripHeadAndTailWhitespace(user_ref_fields[i]);
		if(user_ref.empty())
			continue;

		UserID user_id;
		if(!resolveUserIDFromRef(world_state, user_ref, user_id, error_out))
			return false;

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


static bool postCheckboxIsChecked(const web::RequestInfo& request, const std::string& field_name)
{
	return !request.getPostField(field_name).str().empty();
}


static float parseFloatWithDefault(const web::RequestInfo& request, const std::string& field_name, float default_val)
{
	std::string s = stripHeadAndTailWhitespace(request.getPostField(field_name).str());
	if(s.empty())
		return default_val;
	std::replace(s.begin(), s.end(), ',', '.');

	try
	{
		return (float)stringToDouble(s);
	}
	catch(glare::Exception&)
	{
		return default_val;
	}
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


static std::string toLowerASCII(std::string s)
{
	for(size_t i=0; i<s.size(); ++i)
		s[i] = (char)std::tolower((unsigned char)s[i]);
	return s;
}


static bool containsCaseInsensitive(const std::string& haystack, const std::string& needle)
{
	if(needle.empty())
		return true;
	return toLowerASCII(haystack).find(toLowerASCII(needle)) != std::string::npos;
}


static int parseUIntParamWithBounds(const std::string& value, int default_value, int min_value, int max_value)
{
	const std::string trimmed = stripHeadAndTailWhitespace(value);
	if(trimmed.empty() || !isAllDigitsString(trimmed))
		return default_value;

	const uint64 parsed_u64 = stringToUInt64(trimmed);
	const uint64 max_u64 = (uint64)std::numeric_limits<int>::max();
	const int parsed = (int)myMin(parsed_u64, max_u64);
	return myMin(max_value, myMax(min_value, parsed));
}


static std::string normaliseOrderParam(const std::string& order_value, const std::string& default_order)
{
	const std::string trimmed = toLowerASCII(stripHeadAndTailWhitespace(order_value));
	if(trimmed == "asc" || trimmed == "desc")
		return trimmed;
	return default_order;
}


static int auctionStateSortRank(const bool has_for_sale_auction, const bool has_sold_auction, const bool has_any_auction)
{
	if(has_for_sale_auction) return 3;
	if(has_sold_auction) return 2;
	if(has_any_auction) return 1;
	return 0;
}


struct WorldParcelFilterFields
{
	std::string query_filter;
	std::string owner_filter;
	std::string world_filter;
	std::string auction_filter;
};


static bool worldParcelMatchesFilters(
	const WorldParcelFilterFields& filter,
	const std::string& world_name,
	const Parcel& parcel,
	const std::string& owner_username,
	const std::string& editors_summary,
	const bool has_for_sale_auction,
	const bool has_sold_auction,
	const bool has_any_auction)
{
	bool include = true;
	if(!filter.query_filter.empty())
	{
		const std::string searchable = "parcel " + parcel.id.toString() + " " + owner_username + " " + parcel.description + " " + parcel.title + " " + world_name + " " + editors_summary;
		include = containsCaseInsensitive(searchable, filter.query_filter);
	}
	if(include && !filter.owner_filter.empty())
		include = containsCaseInsensitive(owner_username, filter.owner_filter);
	if(include && !filter.world_filter.empty())
		include = containsCaseInsensitive(world_name, filter.world_filter);
	if(include)
	{
		if(filter.auction_filter == "for_sale")
			include = has_for_sale_auction;
		else if(filter.auction_filter == "sold")
			include = has_sold_auction;
		else if(filter.auction_filter == "without_auction")
			include = !has_any_auction;
	}
	return include;
}


static std::string makeAdminWorldParcelURL(const std::string& world_name, const ParcelID& parcel_id)
{
	return "/admin_world_parcel?world=" + web::Escaping::URLEscape(world_name) + "&parcel_id=" + parcel_id.toString();
}


static std::string makeUserAdminLinkIfFound(ServerAllWorldsState& world_state, const UserID& user_id, const std::string& fallback_text)
{
	const auto user_res = world_state.user_id_to_users.find(user_id);
	if(user_res == world_state.user_id_to_users.end())
		return web::Escaping::HTMLEscape(fallback_text);

	return "<a href=\"/admin_user/" + user_id.toString() + "\">" + web::Escaping::HTMLEscape(user_res->second->name) + "</a>";
}


static WorldParcelFilterFields getWorldParcelFiltersFromPost(const web::RequestInfo& request)
{
	WorldParcelFilterFields filter;
	filter.query_filter = stripHeadAndTailWhitespace(request.getPostField("q").str());
	filter.owner_filter = stripHeadAndTailWhitespace(request.getPostField("owner").str());
	filter.world_filter = stripHeadAndTailWhitespace(request.getPostField("world").str());
	filter.auction_filter = stripHeadAndTailWhitespace(request.getPostField("auction").str());
	if(filter.auction_filter.empty())
		filter.auction_filter = "all";
	return filter;
}


static std::string getSafeRedirectPathOrFallback(const web::RequestInfo& request, const std::string& fallback_path)
{
	const std::string redirect_path = stripHeadAndTailWhitespace(request.getPostField("redirect_path").str());
	if(!redirect_path.empty() && hasPrefix(redirect_path, "/") && !hasPrefix(redirect_path, "//"))
		return redirect_path;
	return fallback_path;
}


std::string sharedAdminHeader(ServerAllWorldsState& world_state, const web::RequestInfo& request_info)
{
	std::string page_out = WebServerResponseUtils::standardHeader(
		world_state,
		request_info,
		/*page title=*/"Metasiberia Admin Site",
		/*extra_header_tags=*/"",
		/*heading_title=*/"Admin");

	page_out += "<p class=\"msb-admin-nav\"><a href=\"/admin\">Main admin page</a><a href=\"/admin_users\">Users</a><a href=\"/admin_parcels\">Parcels</a><a href=\"/admin_world_parcels\">World Parcels</a>";
	page_out += "<a href=\"/admin_parcel_auctions\">Parcel Auctions</a><a href=\"/admin_orders\">Orders</a><a href=\"/admin_sub_eth_transactions\">Eth Transactions</a><a href=\"/admin_map\">Map</a>";
	page_out += "<a href=\"/admin_news_posts\">News Posts</a><a href=\"/admin_lod_chunks\">LOD Chunks</a><a href=\"/admin_worlds\">Worlds</a><a href=\"/admin_chatbots\">ChatBots</a></p>";

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

	page_out += "<p>Welcome!</p>";
	{
		WorldStateLock lock(world_state.mutex);
		Reference<ServerWorldState> root_world = world_state.getRootWorldState();
		const int users_count = (int)world_state.user_id_to_users.size();
		const int worlds_count = (int)world_state.world_states.size();
		const int parcels_count = (int)root_world->parcels.size();
		const int auctions_count = (int)world_state.parcel_auctions.size();

		int chatbots_count = 0;
		int for_sale_auctions = 0;
		const TimeStamp now = TimeStamp::currentTime();

		for(auto it = world_state.parcel_auctions.begin(); it != world_state.parcel_auctions.end(); ++it)
		{
			const ParcelAuction* auction = it->second.ptr();
			if(auction->currentlyForSale(now))
				for_sale_auctions++;
		}

		for(auto it = world_state.world_states.begin(); it != world_state.world_states.end(); ++it)
			chatbots_count += (int)it->second->getChatBots(lock).size();

		page_out += "<div class=\"msb-kpi-grid\">";
		page_out += "<div class=\"msb-kpi\"><div class=\"msb-kpi-label\">Users</div><div class=\"msb-kpi-value\">" + toString(users_count) + "</div></div>";
		page_out += "<div class=\"msb-kpi\"><div class=\"msb-kpi-label\">Worlds</div><div class=\"msb-kpi-value\">" + toString(worlds_count) + "</div></div>";
		page_out += "<div class=\"msb-kpi\"><div class=\"msb-kpi-label\">Parcels</div><div class=\"msb-kpi-value\">" + toString(parcels_count) + "</div></div>";
		page_out += "<div class=\"msb-kpi\"><div class=\"msb-kpi-label\">Auctions for sale</div><div class=\"msb-kpi-value\">" + toString(for_sale_auctions) + "</div></div>";
		page_out += "<div class=\"msb-kpi\"><div class=\"msb-kpi-label\">All auctions</div><div class=\"msb-kpi-value\">" + toString(auctions_count) + "</div></div>";
		page_out += "<div class=\"msb-kpi\"><div class=\"msb-kpi-label\">ChatBots</div><div class=\"msb-kpi-value\">" + toString(chatbots_count) + "</div></div>";
		page_out += "</div>";
	}

	page_out += "<h2>Superadmin parcel tools</h2>";
	page_out += "<div class=\"grouped-region\">";
	page_out += "<p><a href=\"/admin_parcels\">Open parcels admin page</a></p>";

	page_out += "<form action=\"/admin_create_parcel\" method=\"post\" class=\"msb-create-parcel-form\" data-admin-create-parcel-form=\"1\">";
	page_out += "<label for=\"admin-create-root-owner\">Owner username</label><br/>";
	page_out += "<input id=\"admin-create-root-owner\" type=\"text\" name=\"owner_username\" value=\"\" title=\"Username that will own the new root-world parcel\"><br/>";
	page_out += "<label for=\"admin-create-root-editors\">Editors usernames (comma-separated)</label><br/>";
	page_out += "<input id=\"admin-create-root-editors\" type=\"text\" name=\"writer_usernames\" value=\"\" title=\"Comma-separated usernames with edit rights\"><br/>";
	page_out += "<label for=\"admin-create-root-world\">World (name or link, empty=root)</label><br/>";
	page_out += "<input id=\"admin-create-root-world\" type=\"text\" name=\"world_name\" value=\"\" title=\"World name, /world/... URL, or sub://... link\"><br/>";
	page_out += "<div class=\"msb-inline-fields\">";
	page_out += "<label for=\"admin-create-origin-x\">Origin X</label>";
	page_out += "<input id=\"admin-create-origin-x\" type=\"text\" name=\"origin_x\" value=\"0\">";
	page_out += "<label for=\"admin-create-origin-y\">Origin Y</label>";
	page_out += "<input id=\"admin-create-origin-y\" type=\"text\" name=\"origin_y\" value=\"0\">";
	page_out += "</div>";
	page_out += "<div class=\"msb-inline-fields\">";
	page_out += "<label for=\"admin-create-size-x\">Size X</label>";
	page_out += "<input id=\"admin-create-size-x\" type=\"text\" name=\"size_x\" value=\"16\">";
	page_out += "<label for=\"admin-create-size-y\">Size Y</label>";
	page_out += "<input id=\"admin-create-size-y\" type=\"text\" name=\"size_y\" value=\"16\">";
	page_out += "</div>";
	page_out += "<div class=\"msb-inline-fields\">";
	page_out += "<label for=\"admin-create-min-z\">Min Z</label>";
	page_out += "<input id=\"admin-create-min-z\" type=\"text\" name=\"min_z\" value=\"-1\">";
	page_out += "<label for=\"admin-create-max-z\">Max Z</label>";
	page_out += "<input id=\"admin-create-max-z\" type=\"text\" name=\"max_z\" value=\"4\">";
	page_out += "</div>";
	page_out += "<div class=\"msb-live-preview\" data-parcel-live-preview=\"1\">";
	page_out += "<div class=\"msb-live-item\">Area: <b data-preview-area>256.0</b> m<sup>2</sup></div>";
	page_out += "<div class=\"msb-live-item\">Height: <b data-preview-height>5.0</b> m</div>";
	page_out += "<div class=\"msb-live-item\">Build volume: <b data-preview-volume>1280.0</b> m<sup>3</sup></div>";
	page_out += "</div>";
	page_out += "<input type=\"submit\" value=\"Create new parcel\" title=\"Create a parcel using configured world and coordinates\" onclick=\"return confirm('Create parcel with these settings?');\" >";
	page_out += "</form>";

	page_out += "<div class=\"msb-inline-actions\">";
	page_out += "<form onsubmit=\"window.location='/parcel/' + document.getElementById('admin-open-parcel-id').value; return false;\">";
	page_out += "<label for=\"admin-open-parcel-id\">parcel id</label> <input type=\"number\" id=\"admin-open-parcel-id\" value=\"1\" min=\"1\"> ";
	page_out += "<input type=\"submit\" value=\"Open parcel page (edit tools)\">";
	page_out += "</form>";

	page_out += "<form onsubmit=\"window.location='/admin_set_parcel_owner/' + document.getElementById('admin-set-owner-parcel-id').value; return false;\">";
	page_out += "<label for=\"admin-set-owner-parcel-id\">parcel id</label> <input type=\"number\" id=\"admin-set-owner-parcel-id\" value=\"1\" min=\"1\"> ";
	page_out += "<input type=\"submit\" value=\"Open set owner form\">";
	page_out += "</form>";
	page_out += "</div>";
	page_out += "</div>";
	page_out += "<hr/>";

	{ // Lock scope
		WorldStateLock lock(world_state.mutex);
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
		WorldStateLock lock(world_state.mutex);

		page_out += "<h2>Users</h2>\n";
		page_out += "<div class=\"msb-table-wrap\">";
		page_out += "<table class=\"msb-table\" aria-label=\"Users table\">";
		page_out += "<thead><tr><th>ID</th><th>Username</th><th>Email</th><th>Joined</th><th>ETH address</th><th>Personal worlds</th><th>Root parcels</th><th>World parcels</th><th>Actions</th></tr></thead>";
		page_out += "<tbody>";

		struct UserParcelWorldStats
		{
			int personal_worlds_count = 0;
			int root_world_parcels_owned = 0;
			int personal_world_parcels_owned = 0;
			std::vector<std::string> personal_world_names;
		};

		std::map<UserID, UserParcelWorldStats> stats_for_user;

		const Reference<ServerWorldState> root_world = world_state.getRootWorldState();
		for(auto it = root_world->getParcels(lock).begin(); it != root_world->getParcels(lock).end(); ++it)
			stats_for_user[it->second->owner_id].root_world_parcels_owned++;

		for(auto world_it = world_state.world_states.begin(); world_it != world_state.world_states.end(); ++world_it)
		{
			ServerWorldState* world = world_it->second.ptr();
			if(world == root_world.ptr())
				continue;

			UserParcelWorldStats& owner_stats = stats_for_user[world->details.owner_id];
			owner_stats.personal_worlds_count++;
			owner_stats.personal_world_names.push_back(world_it->first);

			for(auto parcel_it = world->getParcels(lock).begin(); parcel_it != world->getParcels(lock).end(); ++parcel_it)
				stats_for_user[parcel_it->second->owner_id].personal_world_parcels_owned++;
		}

		for(auto it = world_state.user_id_to_users.begin(); it != world_state.user_id_to_users.end(); ++it)
		{
			const User* user = it->second.ptr();
			const UserParcelWorldStats* stats = NULL;
			const auto stats_it = stats_for_user.find(user->id);
			if(stats_it != stats_for_user.end())
				stats = &stats_it->second;

			const int personal_worlds_count = stats ? stats->personal_worlds_count : 0;
			const int root_parcels_count = stats ? stats->root_world_parcels_owned : 0;
			const int world_parcels_count = stats ? stats->personal_world_parcels_owned : 0;

			page_out += "<tr>";
			page_out += "<td><a href=\"/admin_user/" + user->id.toString() + "\">" + user->id.toString() + "</a></td>";
			page_out += "<td>" + web::Escaping::HTMLEscape(user->name) + "</td>";
			page_out += "<td>" + web::Escaping::HTMLEscape(user->email_address) + "</td>";
			page_out += "<td>" + web::Escaping::HTMLEscape(user->created_time.timeAgoDescription()) + "</td>";
			page_out += "<td><span class=\"eth-address\">" + web::Escaping::HTMLEscape(user->controlled_eth_address) + "</span></td>";

			std::string personal_worlds_html = toString(personal_worlds_count);
			if(stats)
			{
				const int max_world_links = 3;
				for(int i=0; i<(int)stats->personal_world_names.size() && i < max_world_links; ++i)
				{
					const std::string& world_name = stats->personal_world_names[i];
					personal_worlds_html += "<br/><a href=\"/world/" + WorldHandlers::URLEscapeWorldName(world_name) + "\">" + web::Escaping::HTMLEscape(world_name) + "</a>";
				}
				if((int)stats->personal_world_names.size() > max_world_links)
					personal_worlds_html += "<br/><span class=\"field-description\">+" + toString((int)stats->personal_world_names.size() - max_world_links) + " more</span>";
			}

			page_out += "<td>" + personal_worlds_html + "</td>";
			page_out += "<td>" + toString(root_parcels_count) + "</td>";
			page_out += "<td>" + toString(world_parcels_count) + "</td>";

			page_out += "<td><div class=\"msb-row-actions\">";
			page_out += "<a href=\"/admin_user/" + user->id.toString() + "\">Open user</a>";
			page_out += "<a href=\"/admin_parcels?scope=root&owner=" + web::Escaping::URLEscape(user->name) + "\">Root parcels</a>";
			page_out += "<a href=\"/admin_world_parcels?owner=" + web::Escaping::URLEscape(user->name) + "\">World parcels</a>";
			page_out += "</div></td>";
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
		WorldStateLock lock(world_state.mutex);
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

			const Reference<ServerWorldState> root_world = world_state.getRootWorldState();
			int personal_worlds_count = 0;
			std::vector<std::string> personal_world_names;
			for(auto world_it = world_state.world_states.begin(); world_it != world_state.world_states.end(); ++world_it)
			{
				const ServerWorldState* world = world_it->second.ptr();
				if(world == root_world.ptr())
					continue;
				if(world->details.owner_id == user->id)
				{
					personal_worlds_count++;
					personal_world_names.push_back(world_it->first);
				}
			}

			page_out += "<hr/>";
			page_out += "<h3>Personal worlds</h3>";
			page_out += "<div>Total personal worlds owned: <b>" + toString(personal_worlds_count) + "</b></div>";
			if(personal_world_names.empty())
			{
				page_out += "<div class=\"field-description\">No personal worlds owned by this user.</div>";
			}
			else
			{
				page_out += "<div class=\"msb-row-actions\">";
				for(size_t i=0; i<personal_world_names.size(); ++i)
				{
					const std::string& world_name = personal_world_names[i];
					page_out += "<a href=\"/world/" + WorldHandlers::URLEscapeWorldName(world_name) + "\">" + web::Escaping::HTMLEscape(world_name) + "</a>";
				}
				page_out += "</div>";
			}

			struct UserWorldParcelRow
			{
				std::string world_name;
				ParcelRef parcel;
				double size_x = 0;
				double size_y = 0;
			};

			std::vector<UserWorldParcelRow> world_parcel_rows;
			for(auto world_it = world_state.world_states.begin(); world_it != world_state.world_states.end(); ++world_it)
			{
				ServerWorldState* world = world_it->second.ptr();
				if(world == root_world.ptr())
					continue;

				for(auto parcel_it = world->getParcels(lock).begin(); parcel_it != world->getParcels(lock).end(); ++parcel_it)
				{
					ParcelRef parcel = parcel_it->second;
					if(parcel->owner_id != user->id)
						continue;

					UserWorldParcelRow row;
					row.world_name = world_it->first;
					row.parcel = parcel;
					const Vec3d span = parcel->aabb_max - parcel->aabb_min;
					row.size_x = span.x;
					row.size_y = span.y;
					world_parcel_rows.push_back(row);
				}
			}

			page_out += "<hr/>";
			page_out += "<h3>Parcels in personal worlds</h3>";
			page_out += "<div>Total parcels owned in non-root worlds: <b>" + toString((int)world_parcel_rows.size()) + "</b></div>";
			page_out += "<div class=\"field-description\">These parcels can be managed directly from here (owner/editors/delete) or from <a href=\"/admin_world_parcels?owner=" + web::Escaping::URLEscape(user->name) + "\">Parcels admin (world scope)</a>.</div>";

			if(world_parcel_rows.empty())
			{
				page_out += "<div class=\"field-description\">No world parcels owned by this user.</div>";
			}
			else
			{
				page_out += "<div class=\"msb-table-wrap\">";
				page_out += "<table class=\"msb-table\" aria-label=\"User world parcels table\">";
				page_out += "<thead><tr><th>World</th><th>Parcel ID</th><th>Size</th><th>Z-bounds</th><th>Editors</th><th>Created</th><th>Actions</th></tr></thead><tbody>";
				for(size_t i=0; i<world_parcel_rows.size(); ++i)
				{
					const UserWorldParcelRow& row = world_parcel_rows[i];
					const Parcel* parcel = row.parcel.ptr();
					const std::string detail_url = makeAdminWorldParcelURL(row.world_name, parcel->id);
					const std::string user_page_redirect = "/admin_user/" + user->id.toString();
					page_out += "<tr>";
					page_out += "<td><a href=\"/world/" + WorldHandlers::URLEscapeWorldName(row.world_name) + "\">" + web::Escaping::HTMLEscape(row.world_name) + "</a></td>";
					page_out += "<td><a href=\"" + detail_url + "\">" + parcel->id.toString() + "</a></td>";
					page_out += "<td>" + doubleToStringMaxNDecimalPlaces(row.size_x, 1) + " x " + doubleToStringMaxNDecimalPlaces(row.size_y, 1) + " m</td>";
					page_out += "<td>" + doubleToStringMaxNDecimalPlaces(parcel->zbounds.x, 1) + " .. " + doubleToStringMaxNDecimalPlaces(parcel->zbounds.y, 1) + " m</td>";
					page_out += "<td>" + toString((int)parcel->writer_ids.size()) + "</td>";
					page_out += "<td>" + parcel->created_time.timeAgoDescription() + "</td>";
					page_out += "<td>";
					page_out += "<div class=\"msb-row-actions\">";
					page_out += "<a href=\"" + detail_url + "\">Open details</a>";
					page_out += "<a href=\"/admin_world_parcels?world=" + web::Escaping::URLEscape(row.world_name) + "&owner=" + web::Escaping::URLEscape(user->name) + "\">Open in parcels admin</a>";
					page_out += "<form action=\"/set_world_parcel_owner_post\" method=\"post\" class=\"msb-inline-form\">";
					page_out += "<input type=\"hidden\" name=\"world_name\" value=\"" + web::Escaping::HTMLEscape(row.world_name) + "\">";
					page_out += "<input type=\"hidden\" name=\"parcel_id\" value=\"" + parcel->id.toString() + "\">";
					page_out += "<input type=\"hidden\" name=\"redirect_path\" value=\"" + web::Escaping::HTMLEscape(user_page_redirect) + "\">";
					page_out += "<input type=\"text\" name=\"new_owner_ref\" value=\"" + web::Escaping::HTMLEscape(user->name) + "\" placeholder=\"new owner id/name\">";
					page_out += "<input type=\"submit\" value=\"Set owner\" onclick=\"return confirm('Set owner for world parcel " + parcel->id.toString() + "?');\" >";
					page_out += "</form>";
					page_out += "<form action=\"/set_world_parcel_writers_post\" method=\"post\" class=\"msb-inline-form\">";
					page_out += "<input type=\"hidden\" name=\"world_name\" value=\"" + web::Escaping::HTMLEscape(row.world_name) + "\">";
					page_out += "<input type=\"hidden\" name=\"parcel_id\" value=\"" + parcel->id.toString() + "\">";
					page_out += "<input type=\"hidden\" name=\"redirect_path\" value=\"" + web::Escaping::HTMLEscape(user_page_redirect) + "\">";
					page_out += "<input type=\"text\" name=\"writer_refs\" value=\"\" placeholder=\"editors ids/names\">";
					page_out += "<input type=\"submit\" value=\"Set editors\" onclick=\"return confirm('Set editors for world parcel " + parcel->id.toString() + "?');\" >";
					page_out += "</form>";
					page_out += "<form action=\"/admin_delete_parcel\" method=\"post\" class=\"msb-inline-form\">";
					page_out += "<input type=\"hidden\" name=\"world_name\" value=\"" + web::Escaping::HTMLEscape(row.world_name) + "\">";
					page_out += "<input type=\"hidden\" name=\"parcel_id\" value=\"" + parcel->id.toString() + "\">";
					page_out += "<input type=\"hidden\" name=\"redirect_path\" value=\"" + web::Escaping::HTMLEscape(user_page_redirect) + "\">";
					page_out += "<input type=\"submit\" value=\"Delete\" onclick=\"return confirm('Delete world parcel " + parcel->id.toString() + " in " + web::Escaping::HTMLEscape(row.world_name) + "?');\" >";
					page_out += "</form>";
					page_out += "</div>";
					page_out += "</td>";
					page_out += "</tr>";
				}
				page_out += "</tbody></table></div>";
			}
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

	const std::string query_filter = stripHeadAndTailWhitespace(request.getURLParam("q").str());
	const std::string owner_filter = stripHeadAndTailWhitespace(request.getURLParam("owner").str());
	const std::string world_filter = stripHeadAndTailWhitespace(request.getURLParam("world").str());
	std::string auction_filter = stripHeadAndTailWhitespace(request.getURLParam("auction").str());
	std::string scope_filter = stripHeadAndTailWhitespace(request.getURLParam("scope").str());
	std::string root_sort_key = toLowerASCII(stripHeadAndTailWhitespace(request.getURLParam("root_sort").str()));
	std::string root_sort_order = normaliseOrderParam(request.getURLParam("root_order").str(), "asc");
	std::string world_sort_key = toLowerASCII(stripHeadAndTailWhitespace(request.getURLParam("world_sort").str()));
	std::string world_sort_order = normaliseOrderParam(request.getURLParam("world_order").str(), "asc");
	int root_page = parseUIntParamWithBounds(request.getURLParam("root_page").str(), 1, 1, 1000000);
	int world_page = parseUIntParamWithBounds(request.getURLParam("world_page").str(), 1, 1, 1000000);
	int root_per_page = parseUIntParamWithBounds(request.getURLParam("root_per_page").str(), 50, 10, 500);
	int world_per_page = parseUIntParamWithBounds(request.getURLParam("world_per_page").str(), 50, 10, 500);
	const bool world_parcels_tab = (request.path == "/admin_world_parcels");
	const std::string parcels_page_path = world_parcels_tab ? std::string("/admin_world_parcels") : std::string("/admin_parcels");
	if(auction_filter.empty())
		auction_filter = "all";
	if(scope_filter.empty())
		scope_filter = world_parcels_tab ? "worlds" : "all";
	if(scope_filter != "all" && scope_filter != "root" && scope_filter != "worlds")
		scope_filter = world_parcels_tab ? "worlds" : "all";
	if(world_parcels_tab)
		scope_filter = "worlds";
	if(root_sort_key.empty() || (root_sort_key != "id" && root_sort_key != "owner" && root_sort_key != "created" && root_sort_key != "area" && root_sort_key != "screens" && root_sort_key != "auction"))
		root_sort_key = "id";
	if(world_sort_key.empty() || (world_sort_key != "world" && world_sort_key != "id" && world_sort_key != "owner" && world_sort_key != "editors" && world_sort_key != "created" && world_sort_key != "area" && world_sort_key != "volume" && world_sort_key != "screens" && world_sort_key != "auction"))
		world_sort_key = "world";

	std::string page_out = sharedAdminHeader(world_state, request);
	if(world_parcels_tab)
		page_out += "<h2>Parcels in personal worlds</h2>\n";

	const auto buildParcelsURL = [&](const int root_page_value, const int world_page_value, const std::string& root_sort_value, const std::string& root_order_value, const int root_per_page_value, const std::string& world_sort_value, const std::string& world_order_value, const int world_per_page_value)
	{
		std::string url = parcels_page_path + "?";
		url += "q=" + web::Escaping::URLEscape(query_filter);
		url += "&owner=" + web::Escaping::URLEscape(owner_filter);
		url += "&world=" + web::Escaping::URLEscape(world_filter);
		url += "&auction=" + web::Escaping::URLEscape(auction_filter);
		url += "&scope=" + web::Escaping::URLEscape(scope_filter);
		url += "&root_sort=" + web::Escaping::URLEscape(root_sort_value);
		url += "&root_order=" + web::Escaping::URLEscape(root_order_value);
		url += "&root_per_page=" + toString(root_per_page_value);
		url += "&root_page=" + toString(root_page_value);
		url += "&world_sort=" + web::Escaping::URLEscape(world_sort_value);
		url += "&world_order=" + web::Escaping::URLEscape(world_order_value);
		url += "&world_per_page=" + toString(world_per_page_value);
		url += "&world_page=" + toString(world_page_value);
		return url;
	};

	{ // Lock scope
		WorldStateLock lock(world_state.mutex);

		const bool show_root_section = (scope_filter != "worlds");
		const bool show_world_section = (scope_filter != "root");

		if(show_root_section)
			page_out += "<h2>Root world Parcels</h2>\n";

		Reference<ServerWorldState> root_world = world_state.getRootWorldState();
		const TimeStamp now = TimeStamp::currentTime();

		struct ParcelRow
		{
			const Parcel* parcel = NULL;
			std::string owner_username;
			UserID owner_id;
			bool owner_found = false;
			double size_x = 0;
			double size_y = 0;
			double area = 0;
			bool has_for_sale_auction = false;
			bool has_sold_auction = false;
			bool has_any_auction = false;
		};

		std::vector<ParcelRow> rows;
		rows.reserve(root_world->parcels.size());

		int total_parcels = 0;
		int for_sale_count = 0;
		int sold_count = 0;
		int no_auction_count = 0;
		int with_screenshots_count = 0;
		double total_area = 0.0;

		for(auto it = root_world->parcels.begin(); it != root_world->parcels.end(); ++it)
		{
			const Parcel* parcel = it->second.ptr();
			total_parcels++;

			ParcelRow row;
			row.parcel = parcel;
			row.owner_id = parcel->owner_id;

			auto user_res = world_state.user_id_to_users.find(parcel->owner_id);
			row.owner_found = (user_res != world_state.user_id_to_users.end());
			row.owner_username = row.owner_found ? user_res->second->name : std::string("[No user found]");

			const Vec3d span = parcel->aabb_max - parcel->aabb_min;
			row.size_x = span.x;
			row.size_y = span.y;
			row.area = std::max(0.0, row.size_x * row.size_y);
			total_area += row.area;

			if(!parcel->screenshot_ids.empty())
				with_screenshots_count++;

			for(size_t i=0; i<parcel->parcel_auction_ids.size(); ++i)
			{
				const uint32 auction_id = parcel->parcel_auction_ids[i];
				auto auction_res = world_state.parcel_auctions.find(auction_id);
				if(auction_res == world_state.parcel_auctions.end())
					continue;

				row.has_any_auction = true;
				const ParcelAuction* auction = auction_res->second.ptr();
				if(auction->currentlyForSale(now))
					row.has_for_sale_auction = true;
				if(auction->auction_state == ParcelAuction::AuctionState_Sold)
					row.has_sold_auction = true;
			}

			if(row.has_for_sale_auction) for_sale_count++;
			if(row.has_sold_auction) sold_count++;
			if(!row.has_any_auction) no_auction_count++;

			bool include = true;
			if(scope_filter == "worlds")
				include = false;
			if(include && !world_filter.empty())
				include = false; // Root world has no explicit world name.
			if(include && !query_filter.empty())
			{
				const std::string searchable = "parcel " + row.parcel->id.toString() + " " + row.owner_username + " " + row.parcel->description + " " + row.parcel->title;
				include = containsCaseInsensitive(searchable, query_filter);
			}
			if(include && !owner_filter.empty())
				include = containsCaseInsensitive(row.owner_username, owner_filter);

			if(include)
			{
				if(auction_filter == "for_sale")
					include = row.has_for_sale_auction;
				else if(auction_filter == "sold")
					include = row.has_sold_auction;
				else if(auction_filter == "without_auction")
					include = !row.has_any_auction;
			}

			if(include)
				rows.push_back(row);
		}

		const double average_area = (total_parcels > 0) ? (total_area / (double)total_parcels) : 0.0;
		const bool root_descending = (root_sort_order == "desc");
		std::sort(rows.begin(), rows.end(),
			[&](const ParcelRow& a, const ParcelRow& b)
			{
				int cmp = 0;
				if(root_sort_key == "owner")
					cmp = toLowerASCII(a.owner_username).compare(toLowerASCII(b.owner_username));
				else if(root_sort_key == "created")
					cmp = (a.parcel->created_time.time < b.parcel->created_time.time) ? -1 : ((a.parcel->created_time.time > b.parcel->created_time.time) ? 1 : 0);
				else if(root_sort_key == "area")
					cmp = (a.area < b.area) ? -1 : ((a.area > b.area) ? 1 : 0);
				else if(root_sort_key == "screens")
					cmp = ((int)a.parcel->screenshot_ids.size() < (int)b.parcel->screenshot_ids.size()) ? -1 : (((int)a.parcel->screenshot_ids.size() > (int)b.parcel->screenshot_ids.size()) ? 1 : 0);
				else if(root_sort_key == "auction")
				{
					const int a_rank = auctionStateSortRank(a.has_for_sale_auction, a.has_sold_auction, a.has_any_auction);
					const int b_rank = auctionStateSortRank(b.has_for_sale_auction, b.has_sold_auction, b.has_any_auction);
					cmp = (a_rank < b_rank) ? -1 : ((a_rank > b_rank) ? 1 : 0);
				}
				else // "id"
					cmp = (a.parcel->id.value() < b.parcel->id.value()) ? -1 : ((a.parcel->id.value() > b.parcel->id.value()) ? 1 : 0);

				if(cmp == 0)
					cmp = (a.parcel->id.value() < b.parcel->id.value()) ? -1 : ((a.parcel->id.value() > b.parcel->id.value()) ? 1 : 0);

				return root_descending ? (cmp > 0) : (cmp < 0);
			}
		);
		const int root_filtered_total = (int)rows.size();
		const int root_page_count = myMax(1, (root_filtered_total + root_per_page - 1) / root_per_page);
		root_page = myMin(root_page, root_page_count);
		const int root_start_i = (root_page - 1) * root_per_page;
		const int root_end_i = myMin(root_start_i + root_per_page, root_filtered_total);

		if(show_root_section)
		{
			page_out += "<div class=\"msb-kpi-grid\">";
			page_out += "<div class=\"msb-kpi\"><div class=\"msb-kpi-label\">Parcels total</div><div class=\"msb-kpi-value\">" + toString(total_parcels) + "</div></div>";
			page_out += "<div class=\"msb-kpi\"><div class=\"msb-kpi-label\">Filtered</div><div class=\"msb-kpi-value\" id=\"admin-parcel-filtered-count\">" + toString((int)rows.size()) + "</div></div>";
			page_out += "<div class=\"msb-kpi\"><div class=\"msb-kpi-label\">For sale</div><div class=\"msb-kpi-value\">" + toString(for_sale_count) + "</div></div>";
			page_out += "<div class=\"msb-kpi\"><div class=\"msb-kpi-label\">Sold</div><div class=\"msb-kpi-value\">" + toString(sold_count) + "</div></div>";
			page_out += "<div class=\"msb-kpi\"><div class=\"msb-kpi-label\">Without auctions</div><div class=\"msb-kpi-value\">" + toString(no_auction_count) + "</div></div>";
			page_out += "<div class=\"msb-kpi\"><div class=\"msb-kpi-label\">With screenshots</div><div class=\"msb-kpi-value\">" + toString(with_screenshots_count) + "</div></div>";
			page_out += "<div class=\"msb-kpi\"><div class=\"msb-kpi-label\">Average area</div><div class=\"msb-kpi-value\">" + doubleToStringMaxNDecimalPlaces(average_area, 1) + " m^2</div></div>";
			page_out += "</div>";
		}

		page_out += "<section class=\"grouped-region\">";
		page_out += "<h3>Filters</h3>";
		page_out += "<form action=\"" + parcels_page_path + "\" method=\"get\" class=\"msb-inline-form\">";
		page_out += "<label for=\"admin-parcel-q\">Search</label>";
		page_out += "<input id=\"admin-parcel-q\" type=\"text\" name=\"q\" value=\"" + web::Escaping::HTMLEscape(query_filter) + "\" placeholder=\"Parcel id / owner / title / description\">";
		page_out += "<label for=\"admin-parcel-owner\">Owner</label>";
		page_out += "<input id=\"admin-parcel-owner\" type=\"text\" name=\"owner\" value=\"" + web::Escaping::HTMLEscape(owner_filter) + "\" placeholder=\"username\">";
		page_out += "<label for=\"admin-parcel-world\">World</label>";
		page_out += "<input id=\"admin-parcel-world\" type=\"text\" name=\"world\" value=\"" + web::Escaping::HTMLEscape(world_filter) + "\" placeholder=\"world name (for world parcels)\">";
		page_out += "<label for=\"admin-parcel-auction\">Auction</label>";
		page_out += "<select id=\"admin-parcel-auction\" name=\"auction\">";
		page_out += "<option value=\"all\"" + std::string(auction_filter == "all" ? " selected=\"selected\"" : "") + ">All</option>";
		page_out += "<option value=\"for_sale\"" + std::string(auction_filter == "for_sale" ? " selected=\"selected\"" : "") + ">For sale</option>";
		page_out += "<option value=\"sold\"" + std::string(auction_filter == "sold" ? " selected=\"selected\"" : "") + ">Sold</option>";
		page_out += "<option value=\"without_auction\"" + std::string(auction_filter == "without_auction" ? " selected=\"selected\"" : "") + ">Without auction</option>";
		page_out += "</select>";
		if(!world_parcels_tab)
		{
			page_out += "<label for=\"admin-parcel-scope\">Scope</label>";
			page_out += "<select id=\"admin-parcel-scope\" name=\"scope\">";
			page_out += "<option value=\"all\"" + std::string(scope_filter == "all" ? " selected=\"selected\"" : "") + ">All parcels</option>";
			page_out += "<option value=\"root\"" + std::string(scope_filter == "root" ? " selected=\"selected\"" : "") + ">Root world only</option>";
			page_out += "<option value=\"worlds\"" + std::string(scope_filter == "worlds" ? " selected=\"selected\"" : "") + ">Personal worlds only</option>";
			page_out += "</select>";
		}
		else
		{
			page_out += "<input type=\"hidden\" name=\"scope\" value=\"worlds\">";
		}
		page_out += "<input type=\"hidden\" name=\"root_sort\" value=\"" + web::Escaping::HTMLEscape(root_sort_key) + "\">";
		page_out += "<input type=\"hidden\" name=\"root_order\" value=\"" + web::Escaping::HTMLEscape(root_sort_order) + "\">";
		page_out += "<input type=\"hidden\" name=\"root_per_page\" value=\"" + toString(root_per_page) + "\">";
		page_out += "<input type=\"hidden\" name=\"root_page\" value=\"1\">";
		page_out += "<input type=\"hidden\" name=\"world_sort\" value=\"" + web::Escaping::HTMLEscape(world_sort_key) + "\">";
		page_out += "<input type=\"hidden\" name=\"world_order\" value=\"" + web::Escaping::HTMLEscape(world_sort_order) + "\">";
		page_out += "<input type=\"hidden\" name=\"world_per_page\" value=\"" + toString(world_per_page) + "\">";
		page_out += "<input type=\"hidden\" name=\"world_page\" value=\"1\">";
		page_out += "<input type=\"submit\" value=\"Apply\">";
		page_out += "<a class=\"msb-quiet-link\" href=\"" + parcels_page_path + "\">Reset</a>";
		page_out += "</form>";
		if(show_root_section)
		{
			page_out += "<div class=\"field-description\">Quick local filter (without reload, root table):</div>";
			page_out += "<input id=\"admin-parcel-local-filter\" type=\"text\" placeholder=\"Type to instantly filter current root table\">";
		}
		page_out += "</section>";

		if(show_root_section)
		{
			page_out += "<section class=\"grouped-region\">";
			page_out += "<h3>Bulk screenshot regenerate</h3>";
			page_out += "<form action=\"/admin_regenerate_multiple_parcel_screenshots\" method=\"post\" class=\"msb-inline-form\">";
			page_out += "<label for=\"admin-regen-start-id\">start parcel id</label>";
			page_out += "<input id=\"admin-regen-start-id\" type=\"number\" name=\"start_parcel_id\" value=\"" + toString(0) + "\">";
			page_out += "<label for=\"admin-regen-end-id\">end parcel id</label>";
			page_out += "<input id=\"admin-regen-end-id\" type=\"number\" name=\"end_parcel_id\" value=\"" + toString(10) + "\">";
			page_out += "<input type=\"submit\" value=\"Regenerate/recreate parcel screenshots\" onclick=\"return confirm('Are you sure you want to recreate parcel screenshots?');\" >";
			page_out += "</form>";
			page_out += "</section>";
		}

		page_out += "<section class=\"grouped-region\">";
		page_out += "<h3>Create parcel</h3>";
		page_out += "<form action=\"/admin_create_parcel\" method=\"post\" class=\"msb-create-parcel-form\" data-admin-create-parcel-form=\"1\">";
		page_out += "<div class=\"field-description\">Configure where and how the parcel should be created.</div>";
		page_out += "<label for=\"admin-create-world-name\">World (name or link)</label>";
		page_out += "<input id=\"admin-create-world-name\" type=\"text\" name=\"world_name\" value=\"\" title=\"World name, /world/... URL, or sub://... link. Leave empty for root world\">";
		page_out += "<label for=\"admin-create-owner-username\">Owner username</label>";
		page_out += "<input id=\"admin-create-owner-username\" type=\"text\" name=\"owner_username\" value=\"\" title=\"Username that will own the new parcel\">";
		page_out += "<label for=\"admin-create-writer-usernames\">Editors usernames</label>";
		page_out += "<input id=\"admin-create-writer-usernames\" type=\"text\" name=\"writer_usernames\" value=\"\" title=\"Comma-separated usernames with edit rights\">";
		page_out += "<div class=\"msb-inline-fields\">";
		page_out += "<label for=\"admin-create-origin-x\">Origin X</label>";
		page_out += "<input id=\"admin-create-origin-x\" type=\"text\" name=\"origin_x\" value=\"0\" title=\"Parcel bottom-left X coordinate\">";
		page_out += "<label for=\"admin-create-origin-y\">Origin Y</label>";
		page_out += "<input id=\"admin-create-origin-y\" type=\"text\" name=\"origin_y\" value=\"0\" title=\"Parcel bottom-left Y coordinate\">";
		page_out += "</div>";
		page_out += "<div class=\"msb-inline-fields\">";
		page_out += "<label for=\"admin-create-size-x\">Size X</label>";
		page_out += "<input id=\"admin-create-size-x\" type=\"text\" name=\"size_x\" value=\"16\" title=\"Parcel width on X axis\">";
		page_out += "<label for=\"admin-create-size-y\">Size Y</label>";
		page_out += "<input id=\"admin-create-size-y\" type=\"text\" name=\"size_y\" value=\"16\" title=\"Parcel width on Y axis\">";
		page_out += "</div>";
		page_out += "<div class=\"msb-inline-fields\">";
		page_out += "<label for=\"admin-create-min-z\">Min Z</label>";
		page_out += "<input id=\"admin-create-min-z\" type=\"text\" name=\"min_z\" value=\"-1\" title=\"Minimum parcel build height\">";
		page_out += "<label for=\"admin-create-max-z\">Max Z</label>";
		page_out += "<input id=\"admin-create-max-z\" type=\"text\" name=\"max_z\" value=\"4\" title=\"Maximum parcel build height\">";
		page_out += "</div>";
		page_out += "<div class=\"msb-live-preview\" data-parcel-live-preview=\"1\">";
		page_out += "<div class=\"msb-live-item\">Area: <b data-preview-area>256.0</b> m<sup>2</sup></div>";
		page_out += "<div class=\"msb-live-item\">Height: <b data-preview-height>5.0</b> m</div>";
		page_out += "<div class=\"msb-live-item\">Build volume: <b data-preview-volume>1280.0</b> m<sup>3</sup></div>";
		page_out += "</div>";
		page_out += "<input type=\"submit\" value=\"Create new parcel\" title=\"Create a parcel in selected world using these coordinates\" onclick=\"return confirm('Create parcel with these settings?');\" >";
		page_out += "</form>";
		page_out += "</section>";

		if(show_root_section)
		{
			page_out += "<section class=\"grouped-region\">";
			page_out += "<h3>Parcels list</h3>";
			page_out += "<form action=\"" + parcels_page_path + "\" method=\"get\" class=\"msb-inline-form\">";
			page_out += "<input type=\"hidden\" name=\"q\" value=\"" + web::Escaping::HTMLEscape(query_filter) + "\">";
			page_out += "<input type=\"hidden\" name=\"owner\" value=\"" + web::Escaping::HTMLEscape(owner_filter) + "\">";
			page_out += "<input type=\"hidden\" name=\"world\" value=\"" + web::Escaping::HTMLEscape(world_filter) + "\">";
			page_out += "<input type=\"hidden\" name=\"auction\" value=\"" + web::Escaping::HTMLEscape(auction_filter) + "\">";
			page_out += "<input type=\"hidden\" name=\"scope\" value=\"" + web::Escaping::HTMLEscape(scope_filter) + "\">";
			page_out += "<input type=\"hidden\" name=\"world_sort\" value=\"" + web::Escaping::HTMLEscape(world_sort_key) + "\">";
			page_out += "<input type=\"hidden\" name=\"world_order\" value=\"" + web::Escaping::HTMLEscape(world_sort_order) + "\">";
			page_out += "<input type=\"hidden\" name=\"world_per_page\" value=\"" + toString(world_per_page) + "\">";
			page_out += "<input type=\"hidden\" name=\"world_page\" value=\"" + toString(world_page) + "\">";
			page_out += "<label for=\"admin-root-sort\">Sort</label>";
			page_out += "<select id=\"admin-root-sort\" name=\"root_sort\">";
			page_out += "<option value=\"id\"" + std::string(root_sort_key == "id" ? " selected=\"selected\"" : "") + ">ID</option>";
			page_out += "<option value=\"owner\"" + std::string(root_sort_key == "owner" ? " selected=\"selected\"" : "") + ">Owner</option>";
			page_out += "<option value=\"created\"" + std::string(root_sort_key == "created" ? " selected=\"selected\"" : "") + ">Created</option>";
			page_out += "<option value=\"area\"" + std::string(root_sort_key == "area" ? " selected=\"selected\"" : "") + ">Area</option>";
			page_out += "<option value=\"screens\"" + std::string(root_sort_key == "screens" ? " selected=\"selected\"" : "") + ">Screens</option>";
			page_out += "<option value=\"auction\"" + std::string(root_sort_key == "auction" ? " selected=\"selected\"" : "") + ">Auction</option>";
			page_out += "</select>";
			page_out += "<label for=\"admin-root-order\">Order</label>";
			page_out += "<select id=\"admin-root-order\" name=\"root_order\">";
			page_out += "<option value=\"asc\"" + std::string(root_sort_order == "asc" ? " selected=\"selected\"" : "") + ">Asc</option>";
			page_out += "<option value=\"desc\"" + std::string(root_sort_order == "desc" ? " selected=\"selected\"" : "") + ">Desc</option>";
			page_out += "</select>";
			page_out += "<label for=\"admin-root-per-page\">Per page</label>";
			page_out += "<select id=\"admin-root-per-page\" name=\"root_per_page\">";
			page_out += "<option value=\"25\"" + std::string(root_per_page == 25 ? " selected=\"selected\"" : "") + ">25</option>";
			page_out += "<option value=\"50\"" + std::string(root_per_page == 50 ? " selected=\"selected\"" : "") + ">50</option>";
			page_out += "<option value=\"100\"" + std::string(root_per_page == 100 ? " selected=\"selected\"" : "") + ">100</option>";
			page_out += "<option value=\"200\"" + std::string(root_per_page == 200 ? " selected=\"selected\"" : "") + ">200</option>";
			page_out += "<option value=\"500\"" + std::string(root_per_page == 500 ? " selected=\"selected\"" : "") + ">500</option>";
			page_out += "</select>";
			page_out += "<input type=\"hidden\" name=\"root_page\" value=\"1\">";
			page_out += "<input type=\"submit\" value=\"Apply\">";
			page_out += "</form>";
			page_out += "<div class=\"msb-row-actions\">";
			page_out += "<a href=\"" + buildParcelsURL(1, world_page, root_sort_key, root_sort_order, root_per_page, world_sort_key, world_sort_order, world_per_page) + "\">First</a>";
			page_out += "<a href=\"" + buildParcelsURL(myMax(1, root_page - 1), world_page, root_sort_key, root_sort_order, root_per_page, world_sort_key, world_sort_order, world_per_page) + "\">Prev</a>";
			page_out += "<span>Page " + toString(root_page) + " / " + toString(root_page_count) + "</span>";
			page_out += "<a href=\"" + buildParcelsURL(myMin(root_page_count, root_page + 1), world_page, root_sort_key, root_sort_order, root_per_page, world_sort_key, world_sort_order, world_per_page) + "\">Next</a>";
			page_out += "<a href=\"" + buildParcelsURL(root_page_count, world_page, root_sort_key, root_sort_order, root_per_page, world_sort_key, world_sort_order, world_per_page) + "\">Last</a>";
			page_out += "</div>";
			page_out += "<div class=\"msb-table-wrap\">";
			page_out += "<table class=\"msb-table msb-parcels-table\" aria-label=\"Root world Parcels table\">";
			page_out += "<thead><tr><th>ID</th><th>Owner</th><th>Description</th><th>Size</th><th>Z-bounds</th><th>Area</th><th>Screens</th><th>Auction</th><th>Created</th><th>Actions</th></tr></thead><tbody>";

			for(int i=root_start_i; i<root_end_i; ++i)
			{
				const ParcelRow& row = rows[(size_t)i];
				const Parcel* parcel = row.parcel;

			std::string auction_state;
			if(row.has_for_sale_auction)
				auction_state = "<span class=\"feature-enabled\">for sale</span>";
			else if(row.has_sold_auction)
				auction_state = "<span class=\"feature-disabled\">sold</span>";
			else if(row.has_any_auction)
				auction_state = "history";
			else
				auction_state = "none";

			const std::string searchable = toLowerASCII(parcel->id.toString() + " " + row.owner_username + " " + parcel->description + " " + parcel->title);

			page_out += "<tr data-row-search=\"" + web::Escaping::HTMLEscape(searchable) + "\">";
			page_out += "<td><a href=\"/parcel/" + parcel->id.toString() + "\">" + parcel->id.toString() + "</a></td>";
			if(row.owner_found)
				page_out += "<td><a href=\"/admin_user/" + row.owner_id.toString() + "\">" + web::Escaping::HTMLEscape(row.owner_username) + "</a></td>";
			else
				page_out += "<td>" + web::Escaping::HTMLEscape(row.owner_username) + "</td>";
			page_out += "<td>" + web::Escaping::HTMLEscape(parcel->description.empty() ? std::string("-") : parcel->description.substr(0, 140)) + "</td>";
			page_out += "<td>" + doubleToStringMaxNDecimalPlaces(row.size_x, 1) + " x " + doubleToStringMaxNDecimalPlaces(row.size_y, 1) + " m</td>";
			page_out += "<td>" + doubleToStringMaxNDecimalPlaces(parcel->zbounds.x, 1) + " .. " + doubleToStringMaxNDecimalPlaces(parcel->zbounds.y, 1) + " m</td>";
			page_out += "<td>" + doubleToStringMaxNDecimalPlaces(row.area, 1) + " m^2</td>";
			page_out += "<td>" + toString((int)parcel->screenshot_ids.size()) + "</td>";
			page_out += "<td>" + auction_state + "</td>";
			page_out += "<td>" + parcel->created_time.timeAgoDescription() + "</td>";
			page_out += "<td><div class=\"msb-row-actions\">";
			page_out += "<a href=\"/admin_create_parcel_auction/" + parcel->id.toString() + "\">Create auction</a>";
			page_out += "<a href=\"/parcel/" + parcel->id.toString() + "\">Edit parcel</a>";
			page_out += "<a href=\"/admin_set_parcel_owner/" + parcel->id.toString() + "\">Set owner</a>";
			page_out += "<a href=\"/add_parcel_writer?parcel_id=" + parcel->id.toString() + "\">Manage editors</a>";
			page_out += "<form action=\"/admin_delete_parcel\" method=\"post\" style=\"display: inline;\">";
			page_out += "<input type=\"hidden\" name=\"parcel_id\" value=\"" + parcel->id.toString() + "\">";
			page_out += "<input type=\"submit\" value=\"Delete\" onclick=\"return confirm('Are you sure you want to delete parcel " + parcel->id.toString() + "?');\" >";
			page_out += "</form>";
			page_out += "</div></td>";
				page_out += "</tr>\n";
			}

			page_out += "</tbody></table></div>";
			const int root_shown_start = (root_filtered_total > 0) ? (root_start_i + 1) : 0;
			page_out += "<div id=\"admin-parcels-visible-count\" class=\"field-description\">Showing " + toString(root_shown_start) + "-" + toString(root_end_i) + " of " + toString(root_filtered_total) + " root parcel(s) after server-side filters. Total root parcels: " + toString(total_parcels) + ".</div>";
			page_out += "</section>";
		}

		if(show_world_section)
		{
			struct WorldParcelRow
			{
				std::string world_name;
				ParcelRef parcel;
				std::string owner_username;
				UserID owner_id;
				bool owner_found = false;
				std::string editors_summary;
				int num_editors = 0;
				double origin_x = 0;
				double origin_y = 0;
				double size_x = 0;
				double size_y = 0;
				double area = 0;
				double height = 0;
				double build_volume = 0;
				bool has_for_sale_auction = false;
				bool has_sold_auction = false;
				bool has_any_auction = false;
			};

			std::vector<WorldParcelRow> world_rows;
			int world_total_parcels = 0;
			int world_for_sale_count = 0;
			int world_sold_count = 0;
			int world_no_auction_count = 0;
			int world_with_screenshots_count = 0;
			double world_total_area = 0.0;

			for(auto world_it = world_state.world_states.begin(); world_it != world_state.world_states.end(); ++world_it)
			{
				ServerWorldState* world = world_it->second.ptr();
				if(world == root_world.ptr())
					continue;

				for(auto parcel_it = world->getParcels(lock).begin(); parcel_it != world->getParcels(lock).end(); ++parcel_it)
				{
					ParcelRef parcel = parcel_it->second;
					world_total_parcels++;

					WorldParcelRow row;
					row.world_name = world_it->first;
					row.parcel = parcel;
					row.owner_id = parcel->owner_id;

					const auto user_res = world_state.user_id_to_users.find(parcel->owner_id);
					row.owner_found = (user_res != world_state.user_id_to_users.end());
					row.owner_username = row.owner_found ? user_res->second->name : std::string("[No user found]");

					const Vec3d span = parcel->aabb_max - parcel->aabb_min;
					row.origin_x = parcel->aabb_min.x;
					row.origin_y = parcel->aabb_min.y;
					row.size_x = span.x;
					row.size_y = span.y;
					row.area = std::max(0.0, row.size_x * row.size_y);
					row.height = parcel->zbounds.y - parcel->zbounds.x;
					row.build_volume = std::max(0.0, row.area * row.height);
					world_total_area += row.area;

					row.num_editors = (int)parcel->writer_ids.size();
					for(size_t z=0; z<parcel->writer_ids.size(); ++z)
					{
						const UserID writer_id = parcel->writer_ids[z];
						if(!row.editors_summary.empty())
							row.editors_summary += ", ";

						const auto writer_res = world_state.user_id_to_users.find(writer_id);
						if(writer_res != world_state.user_id_to_users.end())
							row.editors_summary += writer_res->second->name;
						else
							row.editors_summary += writer_id.toString();
					}

					if(!parcel->screenshot_ids.empty())
						world_with_screenshots_count++;

					for(size_t i=0; i<parcel->parcel_auction_ids.size(); ++i)
					{
						const uint32 auction_id = parcel->parcel_auction_ids[i];
						auto auction_res = world_state.parcel_auctions.find(auction_id);
						if(auction_res == world_state.parcel_auctions.end())
							continue;

						row.has_any_auction = true;
						const ParcelAuction* auction = auction_res->second.ptr();
						if(auction->currentlyForSale(now))
							row.has_for_sale_auction = true;
						if(auction->auction_state == ParcelAuction::AuctionState_Sold)
							row.has_sold_auction = true;
					}

					if(row.has_for_sale_auction) world_for_sale_count++;
					if(row.has_sold_auction) world_sold_count++;
					if(!row.has_any_auction) world_no_auction_count++;

					bool include = true;
					if(!query_filter.empty())
					{
						const std::string searchable = "parcel " + row.parcel->id.toString() + " " + row.owner_username + " " + row.parcel->description + " " + row.parcel->title + " " + row.world_name + " " + row.editors_summary;
						include = containsCaseInsensitive(searchable, query_filter);
					}
					if(include && !owner_filter.empty())
						include = containsCaseInsensitive(row.owner_username, owner_filter);
					if(include && !world_filter.empty())
						include = containsCaseInsensitive(row.world_name, world_filter);

					if(include)
					{
						if(auction_filter == "for_sale")
							include = row.has_for_sale_auction;
						else if(auction_filter == "sold")
							include = row.has_sold_auction;
						else if(auction_filter == "without_auction")
							include = !row.has_any_auction;
					}

					if(include)
						world_rows.push_back(row);
				}
			}

			const double world_average_area = (world_total_parcels > 0) ? (world_total_area / (double)world_total_parcels) : 0.0;
			const bool world_descending = (world_sort_order == "desc");
			std::sort(world_rows.begin(), world_rows.end(),
				[&](const WorldParcelRow& a, const WorldParcelRow& b)
				{
					int cmp = 0;
					if(world_sort_key == "world")
						cmp = toLowerASCII(a.world_name).compare(toLowerASCII(b.world_name));
					else if(world_sort_key == "owner")
						cmp = toLowerASCII(a.owner_username).compare(toLowerASCII(b.owner_username));
					else if(world_sort_key == "editors")
						cmp = (a.num_editors < b.num_editors) ? -1 : ((a.num_editors > b.num_editors) ? 1 : 0);
					else if(world_sort_key == "created")
						cmp = (a.parcel->created_time.time < b.parcel->created_time.time) ? -1 : ((a.parcel->created_time.time > b.parcel->created_time.time) ? 1 : 0);
					else if(world_sort_key == "area")
						cmp = (a.area < b.area) ? -1 : ((a.area > b.area) ? 1 : 0);
					else if(world_sort_key == "volume")
						cmp = (a.build_volume < b.build_volume) ? -1 : ((a.build_volume > b.build_volume) ? 1 : 0);
					else if(world_sort_key == "screens")
						cmp = ((int)a.parcel->screenshot_ids.size() < (int)b.parcel->screenshot_ids.size()) ? -1 : (((int)a.parcel->screenshot_ids.size() > (int)b.parcel->screenshot_ids.size()) ? 1 : 0);
					else if(world_sort_key == "auction")
					{
						const int a_rank = auctionStateSortRank(a.has_for_sale_auction, a.has_sold_auction, a.has_any_auction);
						const int b_rank = auctionStateSortRank(b.has_for_sale_auction, b.has_sold_auction, b.has_any_auction);
						cmp = (a_rank < b_rank) ? -1 : ((a_rank > b_rank) ? 1 : 0);
					}
					else // "id"
						cmp = (a.parcel->id.value() < b.parcel->id.value()) ? -1 : ((a.parcel->id.value() > b.parcel->id.value()) ? 1 : 0);

					if(cmp == 0)
					{
						cmp = toLowerASCII(a.world_name).compare(toLowerASCII(b.world_name));
						if(cmp == 0)
							cmp = (a.parcel->id.value() < b.parcel->id.value()) ? -1 : ((a.parcel->id.value() > b.parcel->id.value()) ? 1 : 0);
					}

					return world_descending ? (cmp > 0) : (cmp < 0);
				}
			);
			const int world_filtered_total = (int)world_rows.size();
			const int world_page_count = myMax(1, (world_filtered_total + world_per_page - 1) / world_per_page);
			world_page = myMin(world_page, world_page_count);
			const int world_start_i = (world_page - 1) * world_per_page;
			const int world_end_i = myMin(world_start_i + world_per_page, world_filtered_total);
			const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			const bool superadmin = logged_in_user && isGodUser(logged_in_user->id);
			const std::string current_view_url = buildParcelsURL(root_page, world_page, root_sort_key, root_sort_order, root_per_page, world_sort_key, world_sort_order, world_per_page);

			page_out += "<section class=\"grouped-region\">";
			page_out += "<h3>Parcels in personal worlds</h3>";
			page_out += "<div class=\"msb-kpi-grid\">";
			page_out += "<div class=\"msb-kpi\"><div class=\"msb-kpi-label\">World parcels total</div><div class=\"msb-kpi-value\">" + toString(world_total_parcels) + "</div></div>";
			page_out += "<div class=\"msb-kpi\"><div class=\"msb-kpi-label\">Filtered</div><div class=\"msb-kpi-value\">" + toString((int)world_rows.size()) + "</div></div>";
			page_out += "<div class=\"msb-kpi\"><div class=\"msb-kpi-label\">For sale</div><div class=\"msb-kpi-value\">" + toString(world_for_sale_count) + "</div></div>";
			page_out += "<div class=\"msb-kpi\"><div class=\"msb-kpi-label\">Sold</div><div class=\"msb-kpi-value\">" + toString(world_sold_count) + "</div></div>";
			page_out += "<div class=\"msb-kpi\"><div class=\"msb-kpi-label\">Without auctions</div><div class=\"msb-kpi-value\">" + toString(world_no_auction_count) + "</div></div>";
			page_out += "<div class=\"msb-kpi\"><div class=\"msb-kpi-label\">With screenshots</div><div class=\"msb-kpi-value\">" + toString(world_with_screenshots_count) + "</div></div>";
			page_out += "<div class=\"msb-kpi\"><div class=\"msb-kpi-label\">Average area</div><div class=\"msb-kpi-value\">" + doubleToStringMaxNDecimalPlaces(world_average_area, 1) + " m^2</div></div>";
			page_out += "</div>";
			page_out += "<form action=\"" + parcels_page_path + "\" method=\"get\" class=\"msb-inline-form\">";
			page_out += "<input type=\"hidden\" name=\"q\" value=\"" + web::Escaping::HTMLEscape(query_filter) + "\">";
			page_out += "<input type=\"hidden\" name=\"owner\" value=\"" + web::Escaping::HTMLEscape(owner_filter) + "\">";
			page_out += "<input type=\"hidden\" name=\"world\" value=\"" + web::Escaping::HTMLEscape(world_filter) + "\">";
			page_out += "<input type=\"hidden\" name=\"auction\" value=\"" + web::Escaping::HTMLEscape(auction_filter) + "\">";
			page_out += "<input type=\"hidden\" name=\"scope\" value=\"" + web::Escaping::HTMLEscape(scope_filter) + "\">";
			page_out += "<input type=\"hidden\" name=\"root_sort\" value=\"" + web::Escaping::HTMLEscape(root_sort_key) + "\">";
			page_out += "<input type=\"hidden\" name=\"root_order\" value=\"" + web::Escaping::HTMLEscape(root_sort_order) + "\">";
			page_out += "<input type=\"hidden\" name=\"root_per_page\" value=\"" + toString(root_per_page) + "\">";
			page_out += "<input type=\"hidden\" name=\"root_page\" value=\"" + toString(root_page) + "\">";
			page_out += "<label for=\"admin-world-sort\">Sort</label>";
			page_out += "<select id=\"admin-world-sort\" name=\"world_sort\">";
			page_out += "<option value=\"world\"" + std::string(world_sort_key == "world" ? " selected=\"selected\"" : "") + ">World</option>";
			page_out += "<option value=\"id\"" + std::string(world_sort_key == "id" ? " selected=\"selected\"" : "") + ">ID</option>";
			page_out += "<option value=\"owner\"" + std::string(world_sort_key == "owner" ? " selected=\"selected\"" : "") + ">Owner</option>";
			page_out += "<option value=\"editors\"" + std::string(world_sort_key == "editors" ? " selected=\"selected\"" : "") + ">Editors</option>";
			page_out += "<option value=\"created\"" + std::string(world_sort_key == "created" ? " selected=\"selected\"" : "") + ">Created</option>";
			page_out += "<option value=\"area\"" + std::string(world_sort_key == "area" ? " selected=\"selected\"" : "") + ">Area</option>";
			page_out += "<option value=\"volume\"" + std::string(world_sort_key == "volume" ? " selected=\"selected\"" : "") + ">Volume</option>";
			page_out += "<option value=\"screens\"" + std::string(world_sort_key == "screens" ? " selected=\"selected\"" : "") + ">Screens</option>";
			page_out += "<option value=\"auction\"" + std::string(world_sort_key == "auction" ? " selected=\"selected\"" : "") + ">Auction</option>";
			page_out += "</select>";
			page_out += "<label for=\"admin-world-order\">Order</label>";
			page_out += "<select id=\"admin-world-order\" name=\"world_order\">";
			page_out += "<option value=\"asc\"" + std::string(world_sort_order == "asc" ? " selected=\"selected\"" : "") + ">Asc</option>";
			page_out += "<option value=\"desc\"" + std::string(world_sort_order == "desc" ? " selected=\"selected\"" : "") + ">Desc</option>";
			page_out += "</select>";
			page_out += "<label for=\"admin-world-per-page\">Per page</label>";
			page_out += "<select id=\"admin-world-per-page\" name=\"world_per_page\">";
			page_out += "<option value=\"25\"" + std::string(world_per_page == 25 ? " selected=\"selected\"" : "") + ">25</option>";
			page_out += "<option value=\"50\"" + std::string(world_per_page == 50 ? " selected=\"selected\"" : "") + ">50</option>";
			page_out += "<option value=\"100\"" + std::string(world_per_page == 100 ? " selected=\"selected\"" : "") + ">100</option>";
			page_out += "<option value=\"200\"" + std::string(world_per_page == 200 ? " selected=\"selected\"" : "") + ">200</option>";
			page_out += "<option value=\"500\"" + std::string(world_per_page == 500 ? " selected=\"selected\"" : "") + ">500</option>";
			page_out += "</select>";
			page_out += "<input type=\"hidden\" name=\"world_page\" value=\"1\">";
			page_out += "<input type=\"submit\" value=\"Apply\">";
			page_out += "</form>";
			page_out += "<div class=\"msb-row-actions\">";
			page_out += "<a href=\"" + buildParcelsURL(root_page, 1, root_sort_key, root_sort_order, root_per_page, world_sort_key, world_sort_order, world_per_page) + "\">First</a>";
			page_out += "<a href=\"" + buildParcelsURL(root_page, myMax(1, world_page - 1), root_sort_key, root_sort_order, root_per_page, world_sort_key, world_sort_order, world_per_page) + "\">Prev</a>";
			page_out += "<span>Page " + toString(world_page) + " / " + toString(world_page_count) + "</span>";
			page_out += "<a href=\"" + buildParcelsURL(root_page, myMin(world_page_count, world_page + 1), root_sort_key, root_sort_order, root_per_page, world_sort_key, world_sort_order, world_per_page) + "\">Next</a>";
			page_out += "<a href=\"" + buildParcelsURL(root_page, world_page_count, root_sort_key, root_sort_order, root_per_page, world_sort_key, world_sort_order, world_per_page) + "\">Last</a>";
			page_out += "</div>";

			page_out += "<div class=\"field-description\">Click world, parcel ID, and usernames to open details. Full geometry/owner/editors/delete controls are available on each parcel detail page.</div>";
			page_out += "<div class=\"msb-table-wrap\">";
			page_out += "<table class=\"msb-table\" aria-label=\"Personal worlds parcels table\">";
			page_out += "<thead><tr><th>World</th><th>ID</th><th>Owner</th><th>Editors</th><th>Description</th><th>Origin</th><th>Size</th><th>Z-bounds</th><th>Area</th><th>Volume</th><th>Screens</th><th>Auction</th><th>Created</th><th>Actions</th></tr></thead><tbody>";

			for(int i=world_start_i; i<world_end_i; ++i)
			{
				const WorldParcelRow& row = world_rows[(size_t)i];
				const Parcel* parcel = row.parcel.ptr();

				std::string auction_state;
				if(row.has_for_sale_auction)
					auction_state = "<span class=\"feature-enabled\">for sale</span>";
				else if(row.has_sold_auction)
					auction_state = "<span class=\"feature-disabled\">sold</span>";
				else if(row.has_any_auction)
					auction_state = "history";
				else
					auction_state = "none";

				const std::string detail_url = makeAdminWorldParcelURL(row.world_name, parcel->id);
				const std::string world_url = "/world/" + WorldHandlers::URLEscapeWorldName(row.world_name);
				const std::string owner_html = row.owner_found ?
					("<a href=\"/admin_user/" + row.owner_id.toString() + "\">" + web::Escaping::HTMLEscape(row.owner_username) + "</a>") :
					web::Escaping::HTMLEscape(row.owner_username);

				page_out += "<tr>";
				page_out += "<td><a href=\"" + world_url + "\">" + web::Escaping::HTMLEscape(row.world_name) + "</a></td>";
				page_out += "<td><a href=\"" + detail_url + "\">" + parcel->id.toString() + "</a></td>";
				page_out += "<td>" + owner_html + "</td>";
				page_out += "<td title=\"" + web::Escaping::HTMLEscape(row.editors_summary) + "\">" + toString(row.num_editors) + "</td>";
				page_out += "<td>" + web::Escaping::HTMLEscape(parcel->description.empty() ? std::string("-") : parcel->description.substr(0, 140)) + "</td>";
				page_out += "<td>x=" + doubleToStringMaxNDecimalPlaces(row.origin_x, 1) + ", y=" + doubleToStringMaxNDecimalPlaces(row.origin_y, 1) + "</td>";
				page_out += "<td>" + doubleToStringMaxNDecimalPlaces(row.size_x, 1) + " x " + doubleToStringMaxNDecimalPlaces(row.size_y, 1) + " m</td>";
				page_out += "<td>" + doubleToStringMaxNDecimalPlaces(parcel->zbounds.x, 1) + " .. " + doubleToStringMaxNDecimalPlaces(parcel->zbounds.y, 1) + " m</td>";
				page_out += "<td>" + doubleToStringMaxNDecimalPlaces(row.area, 1) + " m^2</td>";
				page_out += "<td>" + doubleToStringMaxNDecimalPlaces(row.build_volume, 1) + " m^3</td>";
				page_out += "<td>" + toString((int)parcel->screenshot_ids.size()) + "</td>";
				page_out += "<td>" + auction_state + "</td>";
				page_out += "<td>" + parcel->created_time.timeAgoDescription() + "</td>";
				page_out += "<td><div class=\"msb-row-actions\">";
				page_out += "<a href=\"" + world_url + "\">Open world</a>";
				page_out += "<a href=\"" + detail_url + "\">Open parcel details</a>";
				page_out += "<a href=\"" + detail_url + "#quick-actions\">Quick actions</a>";
				page_out += "<a href=\"" + detail_url + "#geometry\">Geometry</a>";
				page_out += "<a href=\"" + detail_url + "#owner\">Owner</a>";
				page_out += "<a href=\"" + detail_url + "#editors\">Editors</a>";
				page_out += "<a href=\"" + detail_url + "#danger\">Delete</a>";
				page_out += "</div></td>";
				page_out += "</tr>\n";
			}

			page_out += "</tbody></table></div>";
			const int shown_start = (world_filtered_total > 0) ? (world_start_i + 1) : 0;
			page_out += "<div class=\"field-description\">Showing " + toString(shown_start) + "-" + toString(world_end_i) + " of " + toString(world_filtered_total) + " personal-world parcel(s) after server-side filters. Total personal-world parcels: " + toString(world_total_parcels) + ".</div>";

			page_out += "<h3>Bulk actions (superadmin)</h3>";
			if(superadmin)
			{
				page_out += "<div class=\"field-description\">Bulk actions apply to the current filtered dataset (q/owner/world/auction). Review filters before applying.</div>";

				page_out += "<form action=\"/admin_bulk_set_world_parcel_owner_post\" method=\"post\" class=\"msb-inline-form\">";
				page_out += "<input type=\"hidden\" name=\"q\" value=\"" + web::Escaping::HTMLEscape(query_filter) + "\">";
				page_out += "<input type=\"hidden\" name=\"owner\" value=\"" + web::Escaping::HTMLEscape(owner_filter) + "\">";
				page_out += "<input type=\"hidden\" name=\"world\" value=\"" + web::Escaping::HTMLEscape(world_filter) + "\">";
				page_out += "<input type=\"hidden\" name=\"auction\" value=\"" + web::Escaping::HTMLEscape(auction_filter) + "\">";
				page_out += "<input type=\"hidden\" name=\"redirect_path\" value=\"" + web::Escaping::HTMLEscape(current_view_url) + "\">";
				page_out += "<label for=\"bulk-world-owner\">Set owner for filtered parcels</label>";
				page_out += "<input id=\"bulk-world-owner\" type=\"text\" name=\"new_owner_ref\" placeholder=\"owner id/name\">";
				page_out += "<input type=\"submit\" value=\"Bulk set owner\" onclick=\"return confirm('Set owner for all filtered personal-world parcels?');\" >";
				page_out += "</form>";

				page_out += "<form action=\"/admin_bulk_set_world_parcel_writers_post\" method=\"post\" class=\"msb-inline-form\">";
				page_out += "<input type=\"hidden\" name=\"q\" value=\"" + web::Escaping::HTMLEscape(query_filter) + "\">";
				page_out += "<input type=\"hidden\" name=\"owner\" value=\"" + web::Escaping::HTMLEscape(owner_filter) + "\">";
				page_out += "<input type=\"hidden\" name=\"world\" value=\"" + web::Escaping::HTMLEscape(world_filter) + "\">";
				page_out += "<input type=\"hidden\" name=\"auction\" value=\"" + web::Escaping::HTMLEscape(auction_filter) + "\">";
				page_out += "<input type=\"hidden\" name=\"redirect_path\" value=\"" + web::Escaping::HTMLEscape(current_view_url) + "\">";
				page_out += "<label for=\"bulk-world-editors\">Set editors for filtered parcels</label>";
				page_out += "<input id=\"bulk-world-editors\" type=\"text\" name=\"writer_refs\" placeholder=\"editors ids/names\">";
				page_out += "<input type=\"submit\" value=\"Bulk set editors\" onclick=\"return confirm('Set editors for all filtered personal-world parcels?');\" >";
				page_out += "</form>";

				page_out += "<form action=\"/admin_bulk_delete_world_parcels_post\" method=\"post\" class=\"msb-inline-form\">";
				page_out += "<input type=\"hidden\" name=\"q\" value=\"" + web::Escaping::HTMLEscape(query_filter) + "\">";
				page_out += "<input type=\"hidden\" name=\"owner\" value=\"" + web::Escaping::HTMLEscape(owner_filter) + "\">";
				page_out += "<input type=\"hidden\" name=\"world\" value=\"" + web::Escaping::HTMLEscape(world_filter) + "\">";
				page_out += "<input type=\"hidden\" name=\"auction\" value=\"" + web::Escaping::HTMLEscape(auction_filter) + "\">";
				page_out += "<input type=\"hidden\" name=\"redirect_path\" value=\"" + web::Escaping::HTMLEscape(current_view_url) + "\">";
				page_out += "<label><input type=\"checkbox\" name=\"allow_all\" value=\"1\"> allow delete when filters are empty</label>";
				page_out += "<input type=\"submit\" value=\"Bulk delete filtered\" onclick=\"return confirm('Delete all filtered personal-world parcels? Parcels with auctions will be skipped.');\" >";
				page_out += "</form>";
			}
			else
			{
				page_out += "<div class=\"field-description\">Bulk actions are available to superadmin only.</div>";
			}
			page_out += "</section>";
		}
	} // End Lock scope

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page_out);
}


void renderAdminWorldParcelPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		const std::string world_name = normaliseWorldNameField(request.getURLParam("world").str());
		const std::string parcel_id_str = stripHeadAndTailWhitespace(request.getURLParam("parcel_id").str());
		if(world_name.empty())
			throw glare::Exception("Missing world name in URL parameter 'world'.");
		if(parcel_id_str.empty() || !isAllDigitsString(parcel_id_str))
			throw glare::Exception("Invalid or missing 'parcel_id'.");

		const ParcelID parcel_id((uint32)stringToUInt64(parcel_id_str));
		if(!parcel_id.valid())
			throw glare::Exception("Invalid parcel id.");

		std::string page_out = sharedAdminHeader(world_state, request);
		page_out += "<h2>Personal-world parcel details</h2>";

		{ // lock scope
			WorldStateLock lock(world_state.mutex);

			auto world_res = world_state.world_states.find(world_name);
			if(world_res == world_state.world_states.end())
				throw glare::Exception("World '" + world_name + "' not found.");

			ServerWorldState* world = world_res->second.ptr();
			auto parcel_res = world->getParcels(lock).find(parcel_id);
			if(parcel_res == world->getParcels(lock).end())
				throw glare::Exception("Parcel " + parcel_id.toString() + " not found in world '" + world_name + "'.");

			const Parcel* parcel = parcel_res->second.ptr();
			const Vec3d span = parcel->aabb_max - parcel->aabb_min;
			const double origin_x = parcel->aabb_min.x;
			const double origin_y = parcel->aabb_min.y;
			const double size_x = span.x;
			const double size_y = span.y;
			const double area = std::max(0.0, size_x * size_y);
			const double height = parcel->zbounds.y - parcel->zbounds.x;
			const double build_volume = std::max(0.0, area * height);
			const TimeStamp now = TimeStamp::currentTime();

			const std::string owner_link_html = makeUserAdminLinkIfFound(world_state, parcel->owner_id, parcel->owner_id.toString());
			std::string owner_name = parcel->owner_id.toString();
			{
				const auto owner_res = world_state.user_id_to_users.find(parcel->owner_id);
				if(owner_res != world_state.user_id_to_users.end())
					owner_name = owner_res->second->name;
			}

			std::string writers_csv;
			std::string writers_html;
			for(size_t i=0; i<parcel->writer_ids.size(); ++i)
			{
				const UserID writer_id = parcel->writer_ids[i];
				std::string writer_ref = writer_id.toString();
				std::string writer_link_html = web::Escaping::HTMLEscape(writer_ref);

				const auto writer_res = world_state.user_id_to_users.find(writer_id);
				if(writer_res != world_state.user_id_to_users.end())
				{
					writer_ref = writer_res->second->name;
					writer_link_html = "<a href=\"/admin_user/" + writer_id.toString() + "\">" + web::Escaping::HTMLEscape(writer_res->second->name) + "</a>";
				}

				if(!writers_csv.empty())
					writers_csv += ", ";
				writers_csv += writer_ref;

				if(!writers_html.empty())
					writers_html += ", ";
				writers_html += writer_link_html;
			}
			if(writers_html.empty())
				writers_html = "-";

			bool has_for_sale_auction = false;
			bool has_sold_auction = false;
			bool has_any_auction = false;
			for(size_t i=0; i<parcel->parcel_auction_ids.size(); ++i)
			{
				const uint32 auction_id = parcel->parcel_auction_ids[i];
				auto auction_res = world_state.parcel_auctions.find(auction_id);
				if(auction_res == world_state.parcel_auctions.end())
					continue;

				has_any_auction = true;
				const ParcelAuction* auction = auction_res->second.ptr();
				if(auction->currentlyForSale(now))
					has_for_sale_auction = true;
				if(auction->auction_state == ParcelAuction::AuctionState_Sold)
					has_sold_auction = true;
			}

			std::string auction_state;
			if(has_for_sale_auction)
				auction_state = "<span class=\"feature-enabled\">for sale</span>";
			else if(has_sold_auction)
				auction_state = "<span class=\"feature-disabled\">sold</span>";
			else if(has_any_auction)
				auction_state = "history";
			else
				auction_state = "none";

			const std::string world_url = "/world/" + WorldHandlers::URLEscapeWorldName(world_name);
			const std::string detail_url = makeAdminWorldParcelURL(world_name, parcel->id);
			const std::string list_url = "/admin_world_parcels?world=" + web::Escaping::URLEscape(world_name);

			page_out += "<section class=\"grouped-region\">";
			page_out += "<div class=\"msb-row-actions\">";
			page_out += "<a href=\"" + list_url + "\">Back to personal-world parcels</a>";
			page_out += "<a href=\"" + world_url + "\">Open world</a>";
			page_out += "<a href=\"" + list_url + "&q=" + web::Escaping::URLEscape(parcel->id.toString()) + "\">Find this parcel in table</a>";
			page_out += "</div>";
			page_out += "<table class=\"msb-table\" aria-label=\"Personal world parcel details\">";
			page_out += "<tbody>";
			page_out += "<tr><th>World</th><td><a href=\"" + world_url + "\">" + web::Escaping::HTMLEscape(world_name) + "</a></td></tr>";
			page_out += "<tr><th>Parcel ID</th><td>" + parcel->id.toString() + "</td></tr>";
			page_out += "<tr><th>Owner</th><td>" + owner_link_html + " (id " + parcel->owner_id.toString() + ")</td></tr>";
			page_out += "<tr><th>Editors</th><td>" + writers_html + "</td></tr>";
			page_out += "<tr><th>Description</th><td>" + web::Escaping::HTMLEscape(parcel->description.empty() ? std::string("-") : parcel->description) + "</td></tr>";
			page_out += "<tr><th>Created</th><td>" + parcel->created_time.RFC822FormatedString() + " (" + parcel->created_time.timeAgoDescription() + ")</td></tr>";
			page_out += "<tr><th>Origin</th><td>x=" + doubleToStringMaxNDecimalPlaces(origin_x, 2) + ", y=" + doubleToStringMaxNDecimalPlaces(origin_y, 2) + "</td></tr>";
			page_out += "<tr><th>Size</th><td>" + doubleToStringMaxNDecimalPlaces(size_x, 2) + " x " + doubleToStringMaxNDecimalPlaces(size_y, 2) + " m</td></tr>";
			page_out += "<tr><th>Z-bounds</th><td>" + doubleToStringMaxNDecimalPlaces(parcel->zbounds.x, 2) + " .. " + doubleToStringMaxNDecimalPlaces(parcel->zbounds.y, 2) + " m</td></tr>";
			page_out += "<tr><th>Area</th><td>" + doubleToStringMaxNDecimalPlaces(area, 2) + " m^2</td></tr>";
			page_out += "<tr><th>Height</th><td>" + doubleToStringMaxNDecimalPlaces(height, 2) + " m</td></tr>";
			page_out += "<tr><th>Build volume</th><td>" + doubleToStringMaxNDecimalPlaces(build_volume, 2) + " m^3</td></tr>";
			page_out += "<tr><th>Screenshots</th><td>" + toString((int)parcel->screenshot_ids.size()) + "</td></tr>";
			page_out += "<tr><th>Auction state</th><td>" + auction_state + "</td></tr>";
			page_out += "</tbody>";
			page_out += "</table>";
			page_out += "</section>";

			page_out += "<section id=\"quick-actions\" class=\"grouped-region\">";
			page_out += "<h3>Quick actions</h3>";
			page_out += "<div class=\"msb-row-actions\">";
			page_out += "<a href=\"" + world_url + "\">Open world page</a>";
			page_out += "<a href=\"/admin_parcel_auctions\">Open parcel auctions list</a>";
			page_out += "</div>";
			page_out += "<div><b>Screenshots:</b> " + toString((int)parcel->screenshot_ids.size()) + "</div>";
			if(parcel->screenshot_ids.empty())
				page_out += "<div class=\"field-description\">No screenshots linked to this parcel yet. Use the action below to create close and far screenshots.</div>";
			else
			{
				page_out += "<div class=\"msb-row-actions\">";
				for(size_t i=0; i<parcel->screenshot_ids.size(); ++i)
					page_out += "<a href=\"/screenshot/" + toString(parcel->screenshot_ids[i]) + "\">Screenshot " + toString(parcel->screenshot_ids[i]) + "</a>";
				page_out += "</div>";
			}
			page_out += "<form action=\"/admin_regenerate_world_parcel_screenshots\" method=\"post\" class=\"msb-inline-form\">";
			page_out += "<input type=\"hidden\" name=\"world_name\" value=\"" + web::Escaping::HTMLEscape(world_name) + "\">";
			page_out += "<input type=\"hidden\" name=\"parcel_id\" value=\"" + parcel->id.toString() + "\">";
			page_out += "<input type=\"hidden\" name=\"redirect_path\" value=\"" + web::Escaping::HTMLEscape(detail_url + "#quick-actions") + "\">";
			page_out += "<input type=\"submit\" value=\"" + std::string(parcel->screenshot_ids.empty() ? "Create parcel screenshots" : "Regenerate parcel screenshots") + "\" onclick=\"return confirm('Create or regenerate screenshots for this parcel?');\" >";
			page_out += "</form>";
			page_out += "<div><b>Auctions linked:</b> " + toString((int)parcel->parcel_auction_ids.size()) + "</div>";
			if(parcel->parcel_auction_ids.empty())
			{
				page_out += "<div class=\"field-description\">No auction records linked. Creating new auctions is currently available only for root-world parcels.</div>";
			}
			else
			{
				page_out += "<div class=\"msb-row-actions\">";
				for(size_t i=0; i<parcel->parcel_auction_ids.size(); ++i)
				{
					const uint32 auction_id = parcel->parcel_auction_ids[i];
					page_out += "<a href=\"/parcel_auction/" + toString(auction_id) + "\">Auction " + toString(auction_id) + "</a>";
					page_out += "<a href=\"/admin_parcel_auction/" + toString(auction_id) + "\">Admin auction " + toString(auction_id) + "</a>";
				}
				page_out += "</div>";
			}
			page_out += "</section>";

			page_out += "<section id=\"geometry\" class=\"grouped-region\">";
			page_out += "<h3>Geometry</h3>";
			page_out += "<form action=\"/set_world_parcel_geometry_post\" method=\"post\" class=\"msb-create-parcel-form\" data-admin-create-parcel-form=\"1\">";
			page_out += "<input type=\"hidden\" name=\"world_name\" value=\"" + web::Escaping::HTMLEscape(world_name) + "\">";
			page_out += "<input type=\"hidden\" name=\"parcel_id\" value=\"" + parcel->id.toString() + "\">";
			page_out += "<input type=\"hidden\" name=\"redirect_path\" value=\"" + web::Escaping::HTMLEscape(detail_url) + "\">";
			page_out += "<div class=\"msb-inline-fields\">";
			page_out += "<label>Origin X</label>";
			page_out += "<input type=\"text\" name=\"origin_x\" value=\"" + doubleToStringMaxNDecimalPlaces(origin_x, 2) + "\">";
			page_out += "<label>Origin Y</label>";
			page_out += "<input type=\"text\" name=\"origin_y\" value=\"" + doubleToStringMaxNDecimalPlaces(origin_y, 2) + "\">";
			page_out += "</div>";
			page_out += "<div class=\"msb-inline-fields\">";
			page_out += "<label>Size X</label>";
			page_out += "<input type=\"text\" name=\"size_x\" value=\"" + doubleToStringMaxNDecimalPlaces(size_x, 2) + "\">";
			page_out += "<label>Size Y</label>";
			page_out += "<input type=\"text\" name=\"size_y\" value=\"" + doubleToStringMaxNDecimalPlaces(size_y, 2) + "\">";
			page_out += "</div>";
			page_out += "<div class=\"msb-inline-fields\">";
			page_out += "<label>Min Z</label>";
			page_out += "<input type=\"text\" name=\"min_z\" value=\"" + doubleToStringMaxNDecimalPlaces(parcel->zbounds.x, 2) + "\">";
			page_out += "<label>Max Z</label>";
			page_out += "<input type=\"text\" name=\"max_z\" value=\"" + doubleToStringMaxNDecimalPlaces(parcel->zbounds.y, 2) + "\">";
			page_out += "</div>";
			page_out += "<div class=\"msb-live-preview\" data-parcel-live-preview=\"1\">";
			page_out += "<div class=\"msb-live-item\">Area: <b data-preview-area>" + doubleToStringMaxNDecimalPlaces(area, 1) + "</b> m<sup>2</sup></div>";
			page_out += "<div class=\"msb-live-item\">Height: <b data-preview-height>" + doubleToStringMaxNDecimalPlaces(height, 1) + "</b> m</div>";
			page_out += "<div class=\"msb-live-item\">Build volume: <b data-preview-volume>" + doubleToStringMaxNDecimalPlaces(build_volume, 1) + "</b> m<sup>3</sup></div>";
			page_out += "</div>";
			page_out += "<input type=\"submit\" value=\"Save geometry\" onclick=\"return confirm('Update geometry for parcel " + parcel->id.toString() + " in world " + web::Escaping::HTMLEscape(world_name) + "?');\" >";
			page_out += "</form>";
			page_out += "</section>";

			page_out += "<section id=\"owner\" class=\"grouped-region\">";
			page_out += "<h3>Owner</h3>";
			page_out += "<form action=\"/set_world_parcel_owner_post\" method=\"post\" class=\"msb-inline-form\">";
			page_out += "<input type=\"hidden\" name=\"world_name\" value=\"" + web::Escaping::HTMLEscape(world_name) + "\">";
			page_out += "<input type=\"hidden\" name=\"parcel_id\" value=\"" + parcel->id.toString() + "\">";
			page_out += "<input type=\"hidden\" name=\"redirect_path\" value=\"" + web::Escaping::HTMLEscape(detail_url) + "\">";
			page_out += "<label for=\"admin-world-parcel-owner-ref\">Owner username or id</label>";
			page_out += "<input id=\"admin-world-parcel-owner-ref\" type=\"text\" name=\"new_owner_ref\" value=\"" + web::Escaping::HTMLEscape(owner_name) + "\" placeholder=\"owner id/name\">";
			page_out += "<input type=\"submit\" value=\"Set owner\" onclick=\"return confirm('Set owner for parcel " + parcel->id.toString() + "?');\" >";
			page_out += "</form>";
			page_out += "</section>";

			page_out += "<section id=\"editors\" class=\"grouped-region\">";
			page_out += "<h3>Editors</h3>";
			page_out += "<form action=\"/set_world_parcel_writers_post\" method=\"post\" class=\"msb-inline-form\">";
			page_out += "<input type=\"hidden\" name=\"world_name\" value=\"" + web::Escaping::HTMLEscape(world_name) + "\">";
			page_out += "<input type=\"hidden\" name=\"parcel_id\" value=\"" + parcel->id.toString() + "\">";
			page_out += "<input type=\"hidden\" name=\"redirect_path\" value=\"" + web::Escaping::HTMLEscape(detail_url) + "\">";
			page_out += "<label for=\"admin-world-parcel-writer-refs\">Editors usernames or ids (comma-separated)</label>";
			page_out += "<input id=\"admin-world-parcel-writer-refs\" type=\"text\" name=\"writer_refs\" value=\"" + web::Escaping::HTMLEscape(writers_csv) + "\" placeholder=\"editors ids/names\">";
			page_out += "<input type=\"submit\" value=\"Set editors\" onclick=\"return confirm('Set editors for parcel " + parcel->id.toString() + "?');\" >";
			page_out += "</form>";
			page_out += "</section>";

			page_out += "<section id=\"danger\" class=\"grouped-region\">";
			page_out += "<h3>Danger zone</h3>";
			page_out += "<form action=\"/admin_delete_parcel\" method=\"post\" class=\"msb-inline-form\">";
			page_out += "<input type=\"hidden\" name=\"world_name\" value=\"" + web::Escaping::HTMLEscape(world_name) + "\">";
			page_out += "<input type=\"hidden\" name=\"parcel_id\" value=\"" + parcel->id.toString() + "\">";
			page_out += "<input type=\"hidden\" name=\"redirect_path\" value=\"" + web::Escaping::HTMLEscape(list_url) + "\">";
			page_out += "<input type=\"submit\" value=\"Delete parcel\" onclick=\"return confirm('Delete parcel " + parcel->id.toString() + " from world " + web::Escaping::HTMLEscape(world_name) + "?');\" >";
			page_out += "</form>";
			page_out += "</section>";
		} // End lock scope

		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page_out);
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("renderAdminWorldParcelPage error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
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

		for(auto it_world = world_state.map_tile_info_for_world.begin(); it_world != world_state.map_tile_info_for_world.end(); ++it_world)
		{
			const std::string& world_name = it_world->first;
			const MapTileInfo& map_tile_info = it_world->second;

			size_t done_count = 0;
			for(auto it = map_tile_info.info.begin(); it != map_tile_info.info.end(); ++it)
			{
				const TileInfo& info = it->second;
				if(info.cur_tile_screenshot.nonNull() && info.cur_tile_screenshot->state == Screenshot::ScreenshotState_done)
					done_count++;
			}

			page_out += "<div class=\"grouped-region\">";
			page_out += "<b>World:</b> " + web::Escaping::HTMLEscape(world_name.empty() ? std::string("(root)") : world_name) + "<br/>";
			page_out += "<b>Tiles:</b> " + toString(map_tile_info.info.size()) + ", <b>done:</b> " + toString(done_count);
			page_out += "</div>";
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

	page_out += "<div class=\"grouped-region\">";
	page_out += "<h3>Bulk profile update (matching filters)</h3>";
	page_out += "<form action=\"/admin_bulk_update_chatbots_post\" method=\"post\" class=\"full-width\">";
	page_out += "<input type=\"hidden\" name=\"world_name\" value=\"" + web::Escaping::HTMLEscape(world_name_filter) + "\">";
	page_out += "<input type=\"hidden\" name=\"username\" value=\"" + web::Escaping::HTMLEscape(username_filter) + "\">";

	page_out += "<div class=\"form-field\">";
	page_out += "<label><input type=\"checkbox\" name=\"apply_model_url\" value=\"1\"> Set avatar model URL</label><br/>";
	page_out += "<input type=\"text\" name=\"model_url\" value=\"\">";
	page_out += "</div>";

	page_out += "<div class=\"form-field\">";
	page_out += "<label><input type=\"checkbox\" name=\"apply_prompt\" value=\"1\"> Replace chatbot custom prompt</label><br/>";
	page_out += "<textarea rows=\"6\" class=\"full-width\" name=\"custom_prompt_part\"></textarea>";
	page_out += "</div>";

	page_out += "<div class=\"form-field\">";
	page_out += "<label><input type=\"checkbox\" name=\"apply_greeting\" value=\"1\"> Update greeting animation</label><br/>";
	page_out += "Name: <input type=\"text\" name=\"greeting_name\" value=\"\"> ";
	page_out += "URL: <input type=\"text\" name=\"greeting_url\" value=\"\"><br/>";
	page_out += "<label><input type=\"checkbox\" name=\"greeting_animate_head\" value=\"1\"> animate head</label> ";
	page_out += "<label><input type=\"checkbox\" name=\"greeting_loop\" value=\"1\"> loop</label> ";
	page_out += "Cooldown (s): <input type=\"number\" step=\"any\" name=\"greeting_cooldown_s\" value=\"8\">";
	page_out += "</div>";

	page_out += "<div class=\"form-field\">";
	page_out += "<label><input type=\"checkbox\" name=\"apply_idle\" value=\"1\"> Update idle animation</label><br/>";
	page_out += "Name: <input type=\"text\" name=\"idle_name\" value=\"\"> ";
	page_out += "URL: <input type=\"text\" name=\"idle_url\" value=\"\"><br/>";
	page_out += "<label><input type=\"checkbox\" name=\"idle_animate_head\" value=\"1\"> animate head</label> ";
	page_out += "<label><input type=\"checkbox\" name=\"idle_loop\" value=\"1\"> loop</label> ";
	page_out += "Interval (s): <input type=\"number\" step=\"any\" name=\"idle_interval_s\" value=\"30\">";
	page_out += "</div>";

	page_out += "<div class=\"form-field\">";
	page_out += "<label><input type=\"checkbox\" name=\"apply_reactive\" value=\"1\"> Update reactive animation</label><br/>";
	page_out += "Name: <input type=\"text\" name=\"reactive_name\" value=\"\"> ";
	page_out += "URL: <input type=\"text\" name=\"reactive_url\" value=\"\"><br/>";
	page_out += "<label><input type=\"checkbox\" name=\"reactive_animate_head\" value=\"1\"> animate head</label> ";
	page_out += "<label><input type=\"checkbox\" name=\"reactive_loop\" value=\"1\"> loop</label> ";
	page_out += "Cooldown (s): <input type=\"number\" step=\"any\" name=\"reactive_cooldown_s\" value=\"6\">";
	page_out += "</div>";

	page_out += "<input type=\"submit\" value=\"Apply bulk chatbot profile\" onclick=\"return confirm('Apply profile update to all matching chatbots?');\" >";
	page_out += "</form>";
	page_out += "</div>";
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
			page_out += "new owner (id or username): <input type=\"text\" name=\"new_owner_ref\" value=\"" + parcel->owner_id.toString() + "\"><br>";
			page_out += "<small>Examples: 42 or Mr.Admin</small><br/>";
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
		const std::string new_owner_ref = stripHeadAndTailWhitespace(request.getPostField("new_owner_ref").str());

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

				std::string resolve_error;
				UserID new_owner_id;
				if(!resolveUserIDFromRef(world_state, new_owner_ref, new_owner_id, resolve_error))
					throw glare::Exception(resolve_error);

				parcel->owner_id = new_owner_id;

				// Set parcel admins and writers to the new user as well.
				parcel->admin_ids  = std::vector<UserID>(1, new_owner_id);
				parcel->writer_ids = std::vector<UserID>(1, new_owner_id);
				world_state.getRootWorldState()->addParcelAsDBDirty(parcel, lock);

				world_state.denormaliseData(); // Update denormalised data which includes parcel owner name

				world_state.markAsChanged();
				const std::string owner_name = getUserNameForID(world_state, new_owner_id);
				world_state.addAdminAuditLogEntry(
					logged_in_user->id,
					logged_in_user->name,
					"Changed owner for parcel " + toString(parcel_id) + " to user " + new_owner_id.toString() + " (" + owner_name + ").");

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
		const std::string world_name = normaliseWorldNameField(request.getPostField("world_name").str());
		const std::string redirect_path = stripHeadAndTailWhitespace(request.getPostField("redirect_path").str());

		{ // Lock scope
			WorldStateLock lock(world_state.mutex);

			User* user = LoginHandlers::getLoggedInUser(world_state, request);
			runtimeCheck(user);

			ServerWorldState* target_world = NULL;
			if(world_name.empty())
			{
				target_world = world_state.getRootWorldState().ptr();
			}
			else
			{
				const auto world_res = world_state.world_states.find(world_name);
				if(world_res == world_state.world_states.end())
					throw glare::Exception("World '" + world_name + "' not found.");
				target_world = world_res->second.ptr();
			}

			auto parcel_res = target_world->getParcels(lock).find(parcel_id);
			if(parcel_res == target_world->getParcels(lock).end())
				throw glare::Exception("Parcel not found in world '" + (world_name.empty() ? std::string("[root world]") : world_name) + "'.");

			ParcelRef parcel = parcel_res->second;
			if(!parcel->parcel_auction_ids.empty())
				throw glare::Exception("Parcel has auction records, delete/terminate auctions first.");

			// Remove from dirty set first so it won't be re-written to DB after delete.
			target_world->getDBDirtyParcels(lock).erase(parcel);

			if(parcel->database_key.valid())
				world_state.db_records_to_delete.insert(parcel->database_key);

			target_world->getParcels(lock).erase(parcel_res);

			world_state.markAsChanged();
			const std::string world_display_name = world_name.empty() ? std::string("[root world]") : world_name;
			world_state.setUserWebMessage(user->id, "Parcel " + parcel_id.toString() + " deleted from world '" + world_display_name + "'.");
			world_state.addAdminAuditLogEntry(user->id, user->name, "Deleted parcel " + parcel_id.toString() + " from world '" + world_display_name + "'.");
		} // End lock scope

		if(!redirect_path.empty() && hasPrefix(redirect_path, "/") && !hasPrefix(redirect_path, "//"))
			web::ResponseUtils::writeRedirectTo(reply_info, redirect_path);
		else if(world_name.empty())
			web::ResponseUtils::writeRedirectTo(reply_info, "/admin_parcels");
		else
			web::ResponseUtils::writeRedirectTo(reply_info, "/admin_world_parcels?world=" + web::Escaping::URLEscape(world_name));
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleDeleteParcelPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


struct WorldParcelMatch
{
	std::string world_name;
	ServerWorldState* world = NULL;
	ParcelRef parcel;
};


static void collectFilteredWorldParcels(ServerAllWorldsState& world_state, const WorldParcelFilterFields& filter, const TimeStamp& now, WorldStateLock& lock, std::vector<WorldParcelMatch>& matches_out)
{
	const Reference<ServerWorldState> root_world = world_state.getRootWorldState();
	for(auto world_it = world_state.world_states.begin(); world_it != world_state.world_states.end(); ++world_it)
	{
		ServerWorldState* world = world_it->second.ptr();
		if(world == root_world.ptr())
			continue;

		for(auto parcel_it = world->getParcels(lock).begin(); parcel_it != world->getParcels(lock).end(); ++parcel_it)
		{
			ParcelRef parcel = parcel_it->second;

			std::string owner_username = "[No user found]";
			const auto user_res = world_state.user_id_to_users.find(parcel->owner_id);
			if(user_res != world_state.user_id_to_users.end())
				owner_username = user_res->second->name;

			std::string editors_summary;
			for(size_t z=0; z<parcel->writer_ids.size(); ++z)
			{
				if(!editors_summary.empty())
					editors_summary += ", ";

				const auto writer_res = world_state.user_id_to_users.find(parcel->writer_ids[z]);
				if(writer_res != world_state.user_id_to_users.end())
					editors_summary += writer_res->second->name;
				else
					editors_summary += parcel->writer_ids[z].toString();
			}

			bool has_for_sale_auction = false;
			bool has_sold_auction = false;
			bool has_any_auction = false;
			for(size_t i=0; i<parcel->parcel_auction_ids.size(); ++i)
			{
				const uint32 auction_id = parcel->parcel_auction_ids[i];
				const auto auction_res = world_state.parcel_auctions.find(auction_id);
				if(auction_res == world_state.parcel_auctions.end())
					continue;

				has_any_auction = true;
				const ParcelAuction* auction = auction_res->second.ptr();
				if(auction->currentlyForSale(now))
					has_for_sale_auction = true;
				if(auction->auction_state == ParcelAuction::AuctionState_Sold)
					has_sold_auction = true;
			}

			if(worldParcelMatchesFilters(filter, world_it->first, *parcel, owner_username, editors_summary, has_for_sale_auction, has_sold_auction, has_any_auction))
				matches_out.push_back({world_it->first, world, parcel});
		}
	}
}


void handleRegenerateWorldParcelScreenshotsPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!LoginHandlers::loggedInUserHasAdminPrivs(world_state, request))
	{
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Access denied sorry.");
		return;
	}

	try
	{
		const std::string world_name = normaliseWorldNameField(request.getPostField("world_name").str());
		const ParcelID parcel_id = ParcelID(request.getPostIntField("parcel_id"));
		const std::string fallback_redirect = makeAdminWorldParcelURL(world_name, parcel_id);
		const std::string redirect_path = getSafeRedirectPathOrFallback(request, fallback_redirect);

		{ // Lock scope
			WorldStateLock lock(world_state.mutex);

			const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			runtimeCheck(logged_in_user);
			if(!isGodUser(logged_in_user->id))
				throw glare::Exception("Only superadmin can regenerate screenshots for personal-world parcels.");

			auto world_res = world_state.world_states.find(world_name);
			if(world_res == world_state.world_states.end())
				throw glare::Exception("World '" + world_name + "' not found.");

			ServerWorldState* world = world_res->second.ptr();
			auto parcel_res = world->getParcels(lock).find(parcel_id);
			if(parcel_res == world->getParcels(lock).end())
				throw glare::Exception("Parcel " + parcel_id.toString() + " not found in world '" + world_name + "'.");

			ParcelRef parcel_ref = parcel_res->second;
			Parcel* parcel = parcel_ref.ptr();
			int created_count = 0;
			int updated_count = 0;

			if(parcel->screenshot_ids.empty())
			{
				uint64 next_shot_id = world_state.getNextScreenshotUID();

				// Create close-in screenshot
				{
					ScreenshotRef shot = new Screenshot();
					shot->id = next_shot_id++;
					parcel->getScreenShotPosAndAngles(shot->cam_pos, shot->cam_angles);
					shot->is_map_tile = false;
					shot->width_px = 650;
					shot->highlight_parcel_id = (int)parcel->id.value();
					shot->created_time = TimeStamp::currentTime();
					shot->state = Screenshot::ScreenshotState_notdone;

					world_state.screenshots[shot->id] = shot;
					parcel->screenshot_ids.push_back(shot->id);
					world_state.addScreenshotAsDBDirty(shot);
					created_count++;
				}

				// Create zoomed-out screenshot
				{
					ScreenshotRef shot = new Screenshot();
					shot->id = next_shot_id++;
					parcel->getFarScreenShotPosAndAngles(shot->cam_pos, shot->cam_angles);
					shot->is_map_tile = false;
					shot->width_px = 650;
					shot->highlight_parcel_id = (int)parcel->id.value();
					shot->created_time = TimeStamp::currentTime();
					shot->state = Screenshot::ScreenshotState_notdone;

					world_state.screenshots[shot->id] = shot;
					parcel->screenshot_ids.push_back(shot->id);
					world_state.addScreenshotAsDBDirty(shot);
					created_count++;
				}

				world->addParcelAsDBDirty(parcel_ref, lock);
			}
			else
			{
				for(size_t z=0; z<parcel->screenshot_ids.size(); ++z)
				{
					const uint64 screenshot_id = parcel->screenshot_ids[z];
					auto shot_res = world_state.screenshots.find(screenshot_id);
					if(shot_res == world_state.screenshots.end())
						continue;

					Screenshot* shot = shot_res->second.ptr();
					if(z == 0)
						parcel->getScreenShotPosAndAngles(shot->cam_pos, shot->cam_angles);
					else
						parcel->getFarScreenShotPosAndAngles(shot->cam_pos, shot->cam_angles);

					shot->is_map_tile = false;
					shot->width_px = 650;
					shot->highlight_parcel_id = (int)parcel->id.value();
					shot->state = Screenshot::ScreenshotState_notdone;
					world_state.addScreenshotAsDBDirty(shot);
					updated_count++;
				}
			}

			if((created_count > 0) || (updated_count > 0))
			{
				world_state.markAsChanged();
				if(created_count > 0)
					world_state.setUserWebMessage(logged_in_user->id, "Created " + toString(created_count) + " screenshot(s) and queued regeneration for parcel " + parcel_id.toString() + " in world '" + world_name + "'.");
				else
					world_state.setUserWebMessage(logged_in_user->id, "Marked " + toString(updated_count) + " screenshot(s) for regeneration for parcel " + parcel_id.toString() + " in world '" + world_name + "'.");
				world_state.addAdminAuditLogEntry(logged_in_user->id, logged_in_user->name, "World parcel screenshots action: world='" + world_name + "', parcel=" + parcel_id.toString() + ", created=" + toString(created_count) + ", regenerated=" + toString(updated_count) + ".");
			}
			else
			{
				world_state.setUserWebMessage(logged_in_user->id, "No screenshots were updated for this parcel.");
			}
		} // End lock scope

		web::ResponseUtils::writeRedirectTo(reply_info, redirect_path);
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleRegenerateWorldParcelScreenshotsPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleBulkSetWorldParcelOwnerPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
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

		const std::string new_owner_ref = stripHeadAndTailWhitespace(request.getPostField("new_owner_ref").str());
		const WorldParcelFilterFields filter = getWorldParcelFiltersFromPost(request);
		const std::string redirect_path = getSafeRedirectPathOrFallback(request, "/admin_world_parcels");

		{ // lock scope
			WorldStateLock lock(world_state.mutex);

			const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			runtimeCheck(logged_in_user);
			if(!isGodUser(logged_in_user->id))
				throw glare::Exception("Only superadmin can execute bulk owner updates.");

			UserID new_owner_id;
			std::string resolve_error;
			if(!resolveUserIDFromRef(world_state, new_owner_ref, new_owner_id, resolve_error))
				throw glare::Exception(resolve_error);

			std::vector<WorldParcelMatch> matches;
			collectFilteredWorldParcels(world_state, filter, TimeStamp::currentTime(), lock, matches);

			int updated = 0;
			for(size_t i=0; i<matches.size(); ++i)
			{
				Parcel* parcel = matches[i].parcel.ptr();
				parcel->owner_id = new_owner_id;
				if(!ContainerUtils::contains(parcel->admin_ids, new_owner_id))
					parcel->admin_ids.push_back(new_owner_id);
				if(!ContainerUtils::contains(parcel->writer_ids, new_owner_id))
					parcel->writer_ids.push_back(new_owner_id);
				matches[i].world->addParcelAsDBDirty(matches[i].parcel, lock);
				updated++;
			}

			if(updated > 0)
			{
				world_state.denormaliseData();
				world_state.markAsChanged();
			}
			world_state.setUserWebMessage(logged_in_user->id, "Bulk owner update complete: " + toString(updated) + " parcel(s) updated.");
			world_state.addAdminAuditLogEntry(
				logged_in_user->id,
				logged_in_user->name,
				"Bulk set world parcel owner: updated=" + toString(updated) + ", owner_ref='" + new_owner_ref + "', q='" + filter.query_filter + "', owner='" + filter.owner_filter + "', world='" + filter.world_filter + "', auction='" + filter.auction_filter + "'.");
		}

		web::ResponseUtils::writeRedirectTo(reply_info, redirect_path);
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleBulkSetWorldParcelOwnerPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleBulkSetWorldParcelWritersPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
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

		const std::string writer_refs = request.getPostField("writer_refs").str();
		const WorldParcelFilterFields filter = getWorldParcelFiltersFromPost(request);
		const std::string redirect_path = getSafeRedirectPathOrFallback(request, "/admin_world_parcels");

		{ // lock scope
			WorldStateLock lock(world_state.mutex);

			const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			runtimeCheck(logged_in_user);
			if(!isGodUser(logged_in_user->id))
				throw glare::Exception("Only superadmin can execute bulk editors updates.");

			std::vector<UserID> base_writer_ids;
			std::string parse_error;
			if(!parseCommaSeparatedUserRefsToIDs(world_state, writer_refs, base_writer_ids, parse_error))
				throw glare::Exception(parse_error);

			std::vector<WorldParcelMatch> matches;
			collectFilteredWorldParcels(world_state, filter, TimeStamp::currentTime(), lock, matches);

			int updated = 0;
			for(size_t i=0; i<matches.size(); ++i)
			{
				Parcel* parcel = matches[i].parcel.ptr();
				std::vector<UserID> writer_ids = base_writer_ids;
				if(!ContainerUtils::contains(writer_ids, parcel->owner_id))
					writer_ids.push_back(parcel->owner_id);
				parcel->admin_ids = writer_ids;
				parcel->writer_ids = writer_ids;
				matches[i].world->addParcelAsDBDirty(matches[i].parcel, lock);
				updated++;
			}

			if(updated > 0)
			{
				world_state.denormaliseData();
				world_state.markAsChanged();
			}
			world_state.setUserWebMessage(logged_in_user->id, "Bulk editors update complete: " + toString(updated) + " parcel(s) updated.");
			world_state.addAdminAuditLogEntry(
				logged_in_user->id,
				logged_in_user->name,
				"Bulk set world parcel editors: updated=" + toString(updated) + ", q='" + filter.query_filter + "', owner='" + filter.owner_filter + "', world='" + filter.world_filter + "', auction='" + filter.auction_filter + "'.");
		}

		web::ResponseUtils::writeRedirectTo(reply_info, redirect_path);
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleBulkSetWorldParcelWritersPost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleBulkDeleteWorldParcelsPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
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

		const WorldParcelFilterFields filter = getWorldParcelFiltersFromPost(request);
		const bool allow_delete_all = (request.getPostField("allow_all") == "1");
		const std::string redirect_path = getSafeRedirectPathOrFallback(request, "/admin_world_parcels");

		if(!allow_delete_all && filter.query_filter.empty() && filter.owner_filter.empty() && filter.world_filter.empty() && (filter.auction_filter == "all"))
			throw glare::Exception("Refusing bulk delete without filters. Add filters or enable 'allow delete when filters are empty'.");

		{ // lock scope
			WorldStateLock lock(world_state.mutex);

			const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			runtimeCheck(logged_in_user);
			if(!isGodUser(logged_in_user->id))
				throw glare::Exception("Only superadmin can execute bulk delete.");

			std::vector<WorldParcelMatch> matches;
			collectFilteredWorldParcels(world_state, filter, TimeStamp::currentTime(), lock, matches);

			int deleted = 0;
			int skipped_with_auctions = 0;
			for(size_t i=0; i<matches.size(); ++i)
			{
				ParcelRef parcel = matches[i].parcel;
				if(!parcel->parcel_auction_ids.empty())
				{
					skipped_with_auctions++;
					continue;
				}

				matches[i].world->getDBDirtyParcels(lock).erase(parcel);
				if(parcel->database_key.valid())
					world_state.db_records_to_delete.insert(parcel->database_key);
				matches[i].world->getParcels(lock).erase(parcel->id);
				deleted++;
			}

			if(deleted > 0)
				world_state.markAsChanged();
			world_state.setUserWebMessage(logged_in_user->id, "Bulk delete complete: deleted=" + toString(deleted) + ", skipped_with_auctions=" + toString(skipped_with_auctions) + ".");
			world_state.addAdminAuditLogEntry(
				logged_in_user->id,
				logged_in_user->name,
				"Bulk deleted world parcels: deleted=" + toString(deleted) + ", skipped_with_auctions=" + toString(skipped_with_auctions) + ", q='" + filter.query_filter + "', owner='" + filter.owner_filter + "', world='" + filter.world_filter + "', auction='" + filter.auction_filter + "'.");
		}

		web::ResponseUtils::writeRedirectTo(reply_info, redirect_path);
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleBulkDeleteWorldParcelsPost error: " + e.what());
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


void handleBulkUpdateChatBotsPost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
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

		const bool apply_model_url = postCheckboxIsChecked(request, "apply_model_url");
		const bool apply_prompt = postCheckboxIsChecked(request, "apply_prompt");
		const bool apply_greeting = postCheckboxIsChecked(request, "apply_greeting");
		const bool apply_idle = postCheckboxIsChecked(request, "apply_idle");
		const bool apply_reactive = postCheckboxIsChecked(request, "apply_reactive");

		std::string model_url = stripHeadAndTailWhitespace(request.getPostField("model_url").str());
		if(model_url.size() > 10000)
			model_url.resize(10000);

		std::string custom_prompt = request.getPostField("custom_prompt_part").str();
		if(custom_prompt.size() > ChatBot::MAX_CUSTOM_PROMPT_PART_SIZE)
			custom_prompt.resize(ChatBot::MAX_CUSTOM_PROMPT_PART_SIZE);

		const auto clamp_anim_time = [](float x) { return std::max(0.f, std::min(x, 3600.f)); };

		std::string greeting_name = stripHeadAndTailWhitespace(request.getPostField("greeting_name").str());
		std::string idle_name = stripHeadAndTailWhitespace(request.getPostField("idle_name").str());
		std::string reactive_name = stripHeadAndTailWhitespace(request.getPostField("reactive_name").str());
		if(greeting_name.size() > ChatBot::MAX_GESTURE_NAME_SIZE) greeting_name.resize(ChatBot::MAX_GESTURE_NAME_SIZE);
		if(idle_name.size() > ChatBot::MAX_GESTURE_NAME_SIZE) idle_name.resize(ChatBot::MAX_GESTURE_NAME_SIZE);
		if(reactive_name.size() > ChatBot::MAX_GESTURE_NAME_SIZE) reactive_name.resize(ChatBot::MAX_GESTURE_NAME_SIZE);

		std::string greeting_url = stripHeadAndTailWhitespace(request.getPostField("greeting_url").str());
		std::string idle_url = stripHeadAndTailWhitespace(request.getPostField("idle_url").str());
		std::string reactive_url = stripHeadAndTailWhitespace(request.getPostField("reactive_url").str());
		if(greeting_url.size() > ChatBot::MAX_GESTURE_URL_SIZE) greeting_url.resize(ChatBot::MAX_GESTURE_URL_SIZE);
		if(idle_url.size() > ChatBot::MAX_GESTURE_URL_SIZE) idle_url.resize(ChatBot::MAX_GESTURE_URL_SIZE);
		if(reactive_url.size() > ChatBot::MAX_GESTURE_URL_SIZE) reactive_url.resize(ChatBot::MAX_GESTURE_URL_SIZE);

		uint32 greeting_flags = 0;
		if(postCheckboxIsChecked(request, "greeting_animate_head"))
			greeting_flags |= SingleGestureSettings::FLAG_ANIMATE_HEAD;
		if(postCheckboxIsChecked(request, "greeting_loop"))
			greeting_flags |= SingleGestureSettings::FLAG_LOOP;

		uint32 idle_flags = 0;
		if(postCheckboxIsChecked(request, "idle_animate_head"))
			idle_flags |= SingleGestureSettings::FLAG_ANIMATE_HEAD;
		if(postCheckboxIsChecked(request, "idle_loop"))
			idle_flags |= SingleGestureSettings::FLAG_LOOP;

		uint32 reactive_flags = 0;
		if(postCheckboxIsChecked(request, "reactive_animate_head"))
			reactive_flags |= SingleGestureSettings::FLAG_ANIMATE_HEAD;
		if(postCheckboxIsChecked(request, "reactive_loop"))
			reactive_flags |= SingleGestureSettings::FLAG_LOOP;

		const float greeting_cooldown_s = clamp_anim_time(parseFloatWithDefault(request, "greeting_cooldown_s", 8.f));
		const float idle_interval_s = clamp_anim_time(parseFloatWithDefault(request, "idle_interval_s", 30.f));
		const float reactive_cooldown_s = clamp_anim_time(parseFloatWithDefault(request, "reactive_cooldown_s", 6.f));

		if(!apply_model_url && !apply_prompt && !apply_greeting && !apply_idle && !apply_reactive)
			throw glare::Exception("No bulk update options selected.");

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

					bool changed = false;

					if(apply_model_url)
					{
						chatbot->avatar_settings.model_url = toURLString(model_url);
						changed = true;
					}
					if(apply_prompt)
					{
						chatbot->custom_prompt_part = custom_prompt;
						changed = true;
					}
					if(apply_greeting)
					{
						chatbot->greeting_gesture_name = greeting_name;
						chatbot->greeting_gesture_URL = toURLString(greeting_url);
						chatbot->greeting_gesture_flags = greeting_flags;
						chatbot->greeting_gesture_cooldown_s = greeting_cooldown_s;
						changed = true;
					}
					if(apply_idle)
					{
						chatbot->idle_gesture_name = idle_name;
						chatbot->idle_gesture_URL = toURLString(idle_url);
						chatbot->idle_gesture_flags = idle_flags;
						chatbot->idle_gesture_interval_s = idle_interval_s;
						changed = true;
					}
					if(apply_reactive)
					{
						chatbot->reactive_gesture_name = reactive_name;
						chatbot->reactive_gesture_URL = toURLString(reactive_url);
						chatbot->reactive_gesture_flags = reactive_flags;
						chatbot->reactive_gesture_cooldown_s = reactive_cooldown_s;
						changed = true;
					}

					if(changed)
					{
						if(chatbot->avatar && apply_model_url)
						{
							chatbot->avatar->avatar_settings = chatbot->avatar_settings;
							chatbot->avatar->other_dirty = true;
						}

						world->addChatBotAsDBDirty(chatbot, lock);
						num_updated++;
					}
				}
			}

			if(num_updated > 0)
				world_state.markAsChanged();

			world_state.setUserWebMessage(logged_in_user->id, "Updated " + toString(num_updated) + " chatbot(s) with bulk profile settings.");
			world_state.addAdminAuditLogEntry(
				logged_in_user->id,
				logged_in_user->name,
				"Bulk updated " + toString(num_updated) + " chatbot(s), world_filter='" + world_name_filter + "', username_filter='" + username_filter +
				"', apply_model_url=" + boolToString(apply_model_url) + ", apply_prompt=" + boolToString(apply_prompt) +
				", apply_greeting=" + boolToString(apply_greeting) + ", apply_idle=" + boolToString(apply_idle) + ", apply_reactive=" + boolToString(apply_reactive) + ".");
		}

		web::ResponseUtils::writeRedirectTo(reply_info, "/admin_chatbots?world_name=" + web::Escaping::URLEscape(world_name_filter) + "&username=" + web::Escaping::URLEscape(username_filter));
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleBulkUpdateChatBotsPost error: " + e.what());
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

			WorldStateLock lock(world_state.mutex);

			// Mark all tile screenshots as not done (for all worlds), and enqueue them so the screenshot bot can regenerate.
			for(auto it_world = world_state.map_tile_info_for_world.begin(); it_world != world_state.map_tile_info_for_world.end(); ++it_world)
			{
				const std::string& world_name = it_world->first;
				MapTileInfo& map_tile_info = it_world->second;

				for(auto it = map_tile_info.info.begin(); it != map_tile_info.info.end(); ++it)
				{
					const Vec3<int> key = it->first;
					TileInfo& tile_info = it->second;
					if(tile_info.cur_tile_screenshot.nonNull())
						tile_info.cur_tile_screenshot->state = Screenshot::ScreenshotState_notdone;

					PendingMapTileScreenshot pending;
					pending.world_name = world_name;
					pending.tile_coords = key;
					world_state.pending_map_tile_screenshots.push_back(pending);
				}

				map_tile_info.db_dirty = true;
			}

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

			WorldStateLock lock(world_state.mutex);

			for(auto it_world = world_state.map_tile_info_for_world.begin(); it_world != world_state.map_tile_info_for_world.end(); ++it_world)
			{
				const std::string& world_name = it_world->first;
				MapTileInfo& map_tile_info = it_world->second;

				for(auto it = map_tile_info.info.begin(); it != map_tile_info.info.end(); ++it)
				{
					const Vec3<int> key = it->first;
					TileInfo& tile_info = it->second;

					tile_info.cur_tile_screenshot = new Screenshot();
					tile_info.cur_tile_screenshot->id = world_state.getNextScreenshotUIDUnlocked(lock);
					tile_info.cur_tile_screenshot->created_time = TimeStamp::currentTime();
					tile_info.cur_tile_screenshot->state = Screenshot::ScreenshotState_notdone;
					tile_info.cur_tile_screenshot->is_map_tile = true;
					tile_info.cur_tile_screenshot->tile_x = key.x;
					tile_info.cur_tile_screenshot->tile_y = key.y;
					tile_info.cur_tile_screenshot->tile_z = key.z;

					PendingMapTileScreenshot pending;
					pending.world_name = world_name;
					pending.tile_coords = key;
					world_state.pending_map_tile_screenshots.push_back(pending);
				}

				map_tile_info.db_dirty = true;
			}

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
