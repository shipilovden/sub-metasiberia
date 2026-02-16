/*=====================================================================
AccountHandlers.cpp
-------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "AccountHandlers.h"


#include <ConPrint.h>
#include "RequestInfo.h"
#include <AESEncryption.h>
#include <Exception.h>
#include <MySocket.h>
#include <Lock.h>
#include <Clock.h>
#include <StringUtils.h>
#include <PlatformUtils.h>
#include <KillThreadMessage.h>
#include <Parser.h>
#include <SHA256.h>
#include <Base64.h>
#include <Keccak256.h>
#include <CryptoRNG.h>
#include <MemMappedFile.h>
#include "Response.h"
#include "WebsiteExcep.h"
#include "Escaping.h"
#include "ResponseUtils.h"
#include "WebServerResponseUtils.h"
#include "WorldHandlers.h"
#include "LoginHandlers.h"
#include "../server/ServerWorldState.h"
#include "../server/UserWebSession.h"
#include "../server/SubEthTransaction.h"
#include "../shared/GestureSettings.h"
#include "../ethereum/Signing.h"
#include "../ethereum/Infura.h"
#include <FileUtils.h>
#include <algorithm>


namespace AccountHandlers
{


void renderUserAccountPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	std::string page;

	{ // lock scope
		WorldStateLock lock(world_state.mutex);

		const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
		if(logged_in_user == NULL)
		{
			page += WebServerResponseUtils::standardHTMLHeader(*world_state.web_data_store, request, "User Account");
			page += "You must be logged in to view your user account page.";
			page += WebServerResponseUtils::standardFooter(request, /*include_email_link=*/true);
			web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
			return;
		}

		page += WebServerResponseUtils::standardHeader(world_state, request, /*page title=*/logged_in_user->name);
		page += "<div class=\"main\">   \n";


		// Display any messages for the user
		const std::string msg_for_user = world_state.getAndRemoveUserWebMessage(logged_in_user->id);
		if(!msg_for_user.empty())
			page += "<div class=\"msg\">" + web::Escaping::HTMLEscape(msg_for_user) + "</div>  \n";


		const std::string user_name = logged_in_user->name;
		const std::string user_initial = user_name.empty() ? "?" : std::string(1, user_name[0]);

		Reference<ServerWorldState> root_world = world_state.getRootWorldState();

		std::string parcels_html;
		int owned_parcel_count = 0;
		for(auto it = root_world->parcels.begin(); it != root_world->parcels.end(); ++it)
		{
			const Parcel* parcel = it->second.ptr();
			if(parcel->owner_id == logged_in_user->id)
			{
				owned_parcel_count++;
				parcels_html += "<div class=\"msb-card\">";
				parcels_html += "<div><a href=\"/parcel/" + parcel->id.toString() + "\">Parcel " + parcel->id.toString() + "</a></div>";
				parcels_html += "<div>" + web::Escaping::HTMLEscape(parcel->description) + "</div>";
				if(parcel->nft_status == Parcel::NFTStatus_NotNFT)
					parcels_html += "<div><a href=\"/make_parcel_into_nft?parcel_id=" + parcel->id.toString() + "\">Mint as a NFT</a></div>";
				parcels_html += "</div>";
			}
		}
		if(parcels_html.empty())
			parcels_html = "<div class=\"msb-card\">You do not own parcels yet.</div>";

		std::string worlds_html;
		int owned_world_count = 0;
		for(auto it = world_state.world_states.begin(); it != world_state.world_states.end(); ++it)
		{
			const ServerWorldState* world = it->second.ptr();
			if(world->details.owner_id == logged_in_user->id)
			{
				owned_world_count++;
				worlds_html += "<div class=\"msb-card\"><a href=\"/world/" + WorldHandlers::URLEscapeWorldName(world->details.name) + "\">" + web::Escaping::HTMLEscape(world->details.name) + "</a></div>";
			}
		}
		if(worlds_html.empty())
			worlds_html = "<div class=\"msb-card\">You don't have personal worlds yet.</div>";

		std::string events_html;
		int created_event_count = 0;
		for(auto it = world_state.events.begin(); it != world_state.events.end(); ++it)
		{
			const SubEvent* event = it->second.ptr();
			if(event->creator_id == logged_in_user->id)
			{
				created_event_count++;
				events_html += "<div class=\"msb-card\"><a href=\"/event/" + toString(event->id) + "\">" + web::Escaping::HTMLEscape(event->title) + "</a></div>";
			}
		}
		if(events_html.empty())
			events_html = "<div class=\"msb-card\">No events created yet.</div>";

		std::string chatbots_html;
		int chatbot_count = 0;
		// Look through all chatbots in all worlds.  NOTE: slow
		for(auto world_it = world_state.world_states.begin(); world_it != world_state.world_states.end(); ++world_it)
		{
			ServerWorldState* world = world_it->second.ptr();
			for(auto it = world->getChatBots(lock).begin(); it != world->getChatBots(lock).end(); ++it)
			{
				const ChatBot* chatbot = it->second.ptr();
				if(chatbot->owner_id == logged_in_user->id)
				{
					chatbot_count++;
					chatbots_html += "<div class=\"msb-card\"><a href=\"/edit_chatbot?chatbot_id=" + toString(chatbot->id) + "\">" + web::Escaping::HTMLEscape(chatbot->name) + " (ID: " + toString(chatbot->id) + ")</a></div>";
				}
			}
		}
		if(chatbots_html.empty())
			chatbots_html = "<div class=\"msb-card\">You haven't created any chatbots.</div>";

		page += "<div class=\"msb-card-grid\">";
		page += "<section class=\"msb-card\">";
		page += "<div class=\"msb-avatar\" title=\"Avatar placeholder\">" + web::Escaping::HTMLEscape(user_initial) + "</div>";
		page += "<h2>" + web::Escaping::HTMLEscape(logged_in_user->name) + "</h2>";
		page += "<div>Joined: " + logged_in_user->created_time.timeAgoDescription() + "</div>";
		page += "<div>Email: " + web::Escaping::HTMLEscape(logged_in_user->email_address) + "</div>";
		page += "<div>Avatar model URL: <code>" + web::Escaping::HTMLEscape(toStdString(logged_in_user->avatar_settings.model_url)) + "</code></div>";
		page += "</section>";

		page += "<section class=\"msb-card\">";
		page += "<h2>Quick actions</h2>";
		page += "<div><a href=\"/create_world\">Create a new world</a></div>";
		page += "<div><a href=\"/new_chatbot\">Create a new ChatBot</a></div>";
		page += "<div><a href=\"/change_password\">Change password</a></div>";
		page += "<div><a href=\"/account_gestures\">Gestures</a></div>";
		page += "<button type=\"button\" class=\"msb-accordion-toggle\" data-accordion-target=\"account-dev-tools\">Developer tools</button>";
		page += "<div id=\"account-dev-tools\" class=\"msb-accordion-panel\">";
		page += "<div><a href=\"/script_log\">Show script log</a></div>";
		page += "<div><a href=\"/secrets\">Show secrets (for Lua scripting)</a></div>";
		page += "</div>";
		page += "</section>";

		page += "<section class=\"msb-card\">";
		page += "<h2>Stats</h2>";
		page += "<div>Parcels: " + toString(owned_parcel_count) + "</div>";
		page += "<div>Worlds: " + toString(owned_world_count) + "</div>";
		page += "<div>Events: " + toString(created_event_count) + "</div>";
		page += "<div>ChatBots: " + toString(chatbot_count) + "</div>";
		page += "</section>";
		page += "</div>";

		page += "<h2>Parcels</h2><div class=\"msb-card-grid\">" + parcels_html + "</div>";
		page += "<h2>Worlds</h2><div class=\"msb-card-grid\">" + worlds_html + "</div>";
		page += "<h2>Events</h2><div class=\"msb-card-grid\">" + events_html + "</div>";
		page += "<h2>ChatBots</h2><div class=\"msb-card-grid\">" + chatbots_html + "</div>";
		page += "<p><a href=\"/new_chatbot\">Create a new ChatBot</a></p>";

		page += "<h2>Ethereum</h2>\n";
		page += "Linked Ethereum address: ";
		if(logged_in_user->controlled_eth_address.empty())
			page += "No address linked.";
		else
			page += "<span class=\"eth-address\">" + web::Escaping::HTMLEscape(logged_in_user->controlled_eth_address) + "</span>";

		page += "<br/><br/>";
		page += "<a href=\"/prove_eth_address_owner\">Link an Ethereum address and prove you own it by signing a message</a>";
		page += "<br/><br/>";
		page += "<a href=\"/prove_parcel_owner_by_nft\">Claim ownership of a parcel on substrata.info based on NFT ownership</a>";
		page += "</div>\n";
	}

	page += WebServerResponseUtils::standardFooter(request, /*include_email_link=*/true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


static std::string escapeAndQuote(const std::string& s)
{
	return "\"" + web::Escaping::HTMLEscape(s) + "\"";
}


void renderGestureSettingsPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	std::string page;

	{ // lock scope
		WorldStateLock lock(world_state.mutex);

		const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
		if(logged_in_user == NULL)
		{
			page += WebServerResponseUtils::standardHTMLHeader(*world_state.web_data_store, request, "Gestures");
			page += "You must be logged in to manage gestures.";
			page += WebServerResponseUtils::standardFooter(request, /*include_email_link=*/true);
			web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
			return;
		}

		page += WebServerResponseUtils::standardHeader(world_state, request, /*page title=*/"Gestures");
		page += "<div class=\"main\">   \n";

		const std::string msg_for_user = world_state.getAndRemoveUserWebMessage(logged_in_user->id);
		if(!msg_for_user.empty())
			page += "<div class=\"msg\">" + web::Escaping::HTMLEscape(msg_for_user) + "</div>  \n";

		page += "<p><a href=\"/account\">Back to account</a></p>\n";

		page += "<h2>Your gestures</h2>\n";
		page += "<p>Note: gesture name should match the animation name stored inside the <code>.subanim</code> file.</p>\n";

		page += "<table>\n";
		page += "<tr><th>Name</th><th>Animation URL</th><th>Animate head</th><th>Loop</th><th>Actions</th></tr>\n";

		for(size_t i=0; i<logged_in_user->gesture_settings.gesture_settings.size(); ++i)
		{
			const SingleGestureSettings& s = logged_in_user->gesture_settings.gesture_settings[i];
			const bool animate_head = (s.flags & SingleGestureSettings::FLAG_ANIMATE_HEAD) != 0;
			const bool loop_anim    = (s.flags & SingleGestureSettings::FLAG_LOOP) != 0;

			page += "<tr>";
			page += "<td>" + web::Escaping::HTMLEscape(s.friendly_name) + "</td>";
			page += "<td><code>" + web::Escaping::HTMLEscape(toStdString(s.anim_URL)) + "</code></td>";
			page += "<td>" + std::string(animate_head ? "yes" : "no") + "</td>";
			page += "<td>" + std::string(loop_anim ? "yes" : "no") + "</td>";
			page += "<td>";
			page += "<form action=\"/account_delete_gesture_post\" method=\"post\" style=\"display:inline;\">";
			page += "<input type=\"hidden\" name=\"gesture_name\" value=" + escapeAndQuote(s.friendly_name) + ">";
			page += "<input type=\"submit\" value=\"Delete\" onclick=\"return confirm('Delete gesture?');\">";
			page += "</form>";
			page += "</td>";
			page += "</tr>\n";
		}

		page += "</table>\n";

		page += "<h2>Add / update gesture</h2>\n";
		page += "<form action=\"/account_add_gesture_post\" method=\"post\" enctype=\"multipart/form-data\">";
		page += "Gesture name: <input type=\"text\" name=\"gesture_name\" required=\"required\"><br/>\n";
		page += "Animate head: <input type=\"checkbox\" name=\"animate_head\" value=\"checked\"><br/>\n";
		page += "Loop: <input type=\"checkbox\" name=\"loop\" value=\"checked\"><br/>\n";
		page += "<br/>\n";
		page += "Option A: paste existing <code>.subanim</code> URL (already on server):<br/>\n";
		page += "<input type=\"text\" name=\"anim_url\" size=\"80\" value=\"\"><br/>\n";
		page += "<br/>\n";
		page += "Option B: upload a new <code>.subanim</code> file:<br/>\n";
		page += "<input type=\"file\" name=\"file\" value=\"\"><br/>\n";
		page += "<br/>\n";
		page += "<input type=\"submit\" value=\"Save gesture\">";
		page += "</form>\n";

		page += "</div>   \n"; // end main div
	}

	page += WebServerResponseUtils::standardFooter(request, /*include_email_link=*/true);
	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void handleAddGesturePost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		const web::UnsafeString gesture_name_field = request.getPostField("gesture_name");
		const web::UnsafeString anim_url_field = request.getPostField("anim_url");
		const bool animate_head = request.getPostField("animate_head") == "checked";
		const bool loop_anim    = request.getPostField("loop") == "checked";

		if(gesture_name_field.empty())
			throw glare::Exception("Gesture name must not be empty.");

		const std::string gesture_name = gesture_name_field.str();
		if(gesture_name.size() > SingleGestureSettings::MAX_NAME_SIZE)
			throw glare::Exception("Gesture name too long.");

		URLString anim_URL;
		bool uploaded_new_file = false;

		if(!anim_url_field.empty())
		{
			const std::string url_str = anim_url_field.str();
			if(url_str.size() > SingleGestureSettings::MAX_NAME_SIZE)
				throw glare::Exception("Animation URL too long.");
			if(::getExtension(url_str) != "subanim")
				throw glare::Exception("anim_url must end with .subanim");

			anim_URL = URLString(url_str.begin(), url_str.end());
		}
		else
		{
			Reference<web::FormField> file_field = request.getPostFieldForNameIfPresent("file");
			if(!(file_field && !file_field->filename.empty() && !file_field->content.empty()))
				throw glare::Exception("Provide anim_url or upload a .subanim file.");

			const std::string filename = file_field->filename.str();
			if(::getExtension(filename) != "subanim")
				throw glare::Exception("Only .subanim uploads are supported via web currently.");

			if(file_field->content.size() > 100 * 1024 * 1024)
				throw glare::Exception("Uploaded file too large.");

			// Write to a temporary file, then import into the server resource system.
			uint8 rnd_bytes[8];
			CryptoRNG::getRandomBytes(rnd_bytes, sizeof(rnd_bytes));
			const std::string rnd = StringUtils::convertByteArrayToHexString(rnd_bytes, sizeof(rnd_bytes));

			const std::string tmp_path = PlatformUtils::getTempDirPath() + "/gesture_upload_" + rnd + ".subanim";
			FileUtils::writeEntireFile(tmp_path, (const char*)file_field->content.data(), file_field->content.size());

			// NOTE: ResourceManager will copy it into /server_resources with a URL based on hash.
			anim_URL = world_state.resource_manager->copyLocalFileToResourceDirAndReturnURL(tmp_path);
			FileUtils::deleteFile(tmp_path);

			uploaded_new_file = true;
		}

		{ // lock scope
			WorldStateLock lock(world_state.mutex);

			User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			if(logged_in_user == NULL)
				throw glare::Exception("You must be logged in.");

			// If user provided a URL, try to mark the resource as present if the file already exists on disk.
			// This helps when resources are copied into /server_resources out-of-band.
			if(!uploaded_new_file)
			{
				// If the resource isn't present in the map yet, but the file exists, mark it present.
				if(!world_state.resource_manager->isFileForURLPresent(anim_URL))
				{
					const std::string abs_path = world_state.resource_manager->pathForURL(anim_URL);
					if(FileUtils::fileExists(abs_path))
						world_state.resource_manager->setResourceAsLocallyPresentForURL(anim_URL);
				}
			}

			// Persist resource metadata so it survives restart.
			const ResourceRef res = world_state.resource_manager->getExistingResourceForURL(anim_URL);
			if(res.nonNull())
				world_state.addResourceAsDBDirty(res);

			// Update or insert gesture.
			bool updated_existing = false;
			for(size_t i=0; i<logged_in_user->gesture_settings.gesture_settings.size(); ++i)
			{
				SingleGestureSettings& s = logged_in_user->gesture_settings.gesture_settings[i];
				if(s.friendly_name == gesture_name)
				{
					s.anim_URL = anim_URL;
					s.flags = (animate_head ? SingleGestureSettings::FLAG_ANIMATE_HEAD : 0) | (loop_anim ? SingleGestureSettings::FLAG_LOOP : 0);
					updated_existing = true;
					break;
				}
			}

			if(!updated_existing)
			{
				if(logged_in_user->gesture_settings.gesture_settings.size() >= GestureSettings::MAX_GESTURE_SETTINGS_SIZE)
					throw glare::Exception("Too many gestures.");

				SingleGestureSettings s;
				s.friendly_name = gesture_name;
				s.anim_URL = anim_URL;
				s.flags = (animate_head ? SingleGestureSettings::FLAG_ANIMATE_HEAD : 0) | (loop_anim ? SingleGestureSettings::FLAG_LOOP : 0);
				s.anim_duration = 1.0f;

				logged_in_user->gesture_settings.gesture_settings.push_back(s);
			}

			world_state.addUserAsDBDirty(logged_in_user);
			world_state.markAsChanged();
			world_state.setUserWebMessage(logged_in_user->id, "Saved gesture '" + gesture_name + "'. URL: " + toStdString(anim_URL));
		}

		web::ResponseUtils::writeRedirectTo(reply_info, "/account_gestures");
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleAddGesturePost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void handleDeleteGesturePost(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		const web::UnsafeString gesture_name_field = request.getPostField("gesture_name");
		if(gesture_name_field.empty())
			throw glare::Exception("Missing gesture_name");

		const std::string gesture_name = gesture_name_field.str();

		{ // lock scope
			WorldStateLock lock(world_state.mutex);

			User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
			if(logged_in_user == NULL)
				throw glare::Exception("You must be logged in.");

			auto& v = logged_in_user->gesture_settings.gesture_settings;
			const size_t old_size = v.size();
			v.erase(std::remove_if(v.begin(), v.end(), [&](const SingleGestureSettings& s) { return s.friendly_name == gesture_name; }), v.end());

			if(v.size() == old_size)
				world_state.setUserWebMessage(logged_in_user->id, "Gesture not found.");
			else
				world_state.setUserWebMessage(logged_in_user->id, "Deleted gesture '" + gesture_name + "'.");

			world_state.addUserAsDBDirty(logged_in_user);
			world_state.markAsChanged();
		}

		web::ResponseUtils::writeRedirectTo(reply_info, "/account_gestures");
	}
	catch(glare::Exception& e)
	{
		if(!request.fuzzing)
			conPrint("handleDeleteGesturePost error: " + e.what());
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Error: " + e.what());
	}
}


void renderProveEthAddressOwnerPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	std::string page;

	{ // lock scope
		Lock lock(world_state.mutex);

		User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
		if(logged_in_user == NULL)
		{
			page += WebServerResponseUtils::standardHTMLHeader(*world_state.web_data_store, request, "User Account");
			page += "You must be logged in to view your user account page.";
			page += WebServerResponseUtils::standardFooter(request, /*include_email_link=*/true);
			web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
			return;
		}

		// Generate a new random current_eth_signing_nonce
		try
		{
			const int NUM_BYTES = 16;
			uint8 data[NUM_BYTES];

			CryptoRNG::getRandomBytes(data, NUM_BYTES); // throws glare::Exception on failure

			logged_in_user->current_eth_signing_nonce = StringUtils::convertByteArrayToHexString(data, NUM_BYTES);
		}
		catch(glare::Exception& )
		{
			return;
		}

		page += WebServerResponseUtils::standardHeader(world_state, request, /*page title=*/logged_in_user->name);
		page += "<div class=\"main\">   \n";
	
		

		page += "<br/>";
		page += "Step 1: connect to Ethereum/MetaMask";
		page += "<div><button class=\"enableEthereumButton\">Connect to Ethereum/MetaMask</button></div>";

		page += "<br/>";
		page += "<div>Ethereum/MetaMask connection status: <div class=\"metamask-status-div\"></div></div>";

		page += "<br/>";
		page += "<br/>";
		page += "Step 2: Prove you own the Ethereum address by signing a message";
		page += "<div><button class=\"signEthereumButton\">Sign a message</button></div>";

		page += "</div>   \n"; // End main div

		page += "<div id=\"current_eth_signing_nonce\" class=\"hidden\">" + logged_in_user->current_eth_signing_nonce + "</div>\n";

		page += "<script src=\"/files/account.js\"></script>";
	}

	page += WebServerResponseUtils::standardFooter(request, /*include_email_link=*/true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void renderProveParcelOwnerByNFT(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	std::string page;

	{ // lock scope
		Lock lock(world_state.mutex);

		User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
		if(logged_in_user == NULL)
		{
			page += WebServerResponseUtils::standardHTMLHeader(*world_state.web_data_store, request, "User Account");
			page += "You must be logged in to view this page.";
			page += WebServerResponseUtils::standardFooter(request, /*include_email_link=*/true);
			web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
			return;
		}

		page += WebServerResponseUtils::standardHeader(world_state, request, /*page title=*/"Prove parcel ownership via NFT ownership");
		page += "<div class=\"main\">   \n";

		if(logged_in_user->controlled_eth_address.empty())
		{
			page += "You must link an ethereum address to your account first.  You can do that <a href=\"/prove_eth_address_owner\">here</a>.";
		}
		else
		{
			page += "<p>If your Ethereum account is the owner of a Substrata Parcel NFT, you can claim ownership of the parcel on the substrata server on this page.</p>";

			page += "<p>Your linked Ethereum address: <span class=\"eth-address\">" +
				logged_in_user->controlled_eth_address + "</span> (The parcel NFT must be owned by this address)</p>";

			page +=
				"	<form action=\"/claim_parcel_owner_by_nft_post\" method=\"post\">																\n"
				"		<label for=\"parcel-id\">Parcel number:</label><br>																					 \n"
				"		<input id=\"parcel-id\" type=\"number\" name=\"parcel_id\" />			\n"
				"		<button type=\"submit\" id=\"claim-parcel-button\" class=\"button-link\">Claim Parcel</button>			\n"
				"	</form>";

			page += "<p>Please don't spam this button, it does an Ethereum query!  It may take a few seconds to return a response.</p>";
		}

		page += "</div>   \n"; // End main div
	}

	page += WebServerResponseUtils::standardFooter(request, /*include_email_link=*/true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void handleEthSignMessagePost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	 const web::UnsafeString address = request_info.getURLParam("address");
	 const web::UnsafeString sig = request_info.getURLParam("sig");

	 { // lock scope
		 Lock lock(world_state.mutex);

		 User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request_info);
		 if(logged_in_user == NULL)
		 {
			// page += WebServerResponseUtils::standardHTMLHeader(request, "User Account");
			// page += "You must be logged in to view your user account page.";
			 web::ResponseUtils::writeRedirectTo(reply_info, "/account");
			 return;
		 }

		 if(logged_in_user->current_eth_signing_nonce == "") // current_eth_signing_nonce must be non-empty.  This should be the case if submitted in the usual way.
			 return;

		 const std::string message = "Please sign this message to confirm you own the Ethereum account.\n(Unique string: " + logged_in_user->current_eth_signing_nonce + ")";
		 
		 try
		 {
			 const EthAddress recovered_address = Signing::ecrecover(sig.str(), message);

			 if(recovered_address.toHexStringWith0xPrefix() == address.str())
			 {
				 // The user has proved that they control the account with the given address.

				 logged_in_user->controlled_eth_address = recovered_address.toHexStringWith0xPrefix();
				 world_state.addUserAsDBDirty(logged_in_user);

				 web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "{\"msg\":\"Congrats, you have sucessfully proven you control the Ethereum address " + recovered_address.toHexStringWith0xPrefix() + 
					 ". You will now be redirected to your account page.\", \"redirect_URL\":\"/account\"}");
			 }
			 else
			 {
				 web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "{\"msg\":\"Sorry, we could not confirm you control the Ethereum address.\"}");
			 }
		 }
		 catch(glare::Exception& e)
		 {
			 if(!request_info.fuzzing)
				conPrint("Excep while calling ecrecover(): " + e.what());
			 web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "{\"msg\":\"Sorry, we could not confirm you control the Ethereum address.\"}");
		 }
	 }
}



void renderMakeParcelIntoNFTPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	std::string page;

	const ParcelID parcel_id(request.getURLIntParam("parcel_id"));

	{ // lock scope
		Lock lock(world_state.mutex);

		User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
		if(logged_in_user == NULL)
		{
			page += WebServerResponseUtils::standardHTMLHeader(*world_state.web_data_store, request, "User Account");
			page += "You must be logged in to view this page.";
			page += WebServerResponseUtils::standardFooter(request, /*include_email_link=*/true);
			web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
			return;
		}

		// Lookup parcel
		auto res = world_state.getRootWorldState()->parcels.find(parcel_id);
		if(res == world_state.getRootWorldState()->parcels.end())
			throw glare::Exception("No such parcel");
		
		const Parcel* parcel = res->second.ptr();

		if(parcel->owner_id != logged_in_user->id)
			throw glare::Exception("Parcel must be owned by user");

		page += WebServerResponseUtils::standardHeader(world_state, request, /*page title=*/"Convert parcel " + parcel_id.toString() + " to a NFT");
		page += "<div class=\"main\">   \n";

		if(parcel->nft_status == Parcel::NFTStatus_NotNFT)
		{
			if(logged_in_user->controlled_eth_address.empty())
			{
				page += "You must link an ethereum address to your account first.  You can do that <a href=\"/prove_eth_address_owner\">here</a>.";
			}
			else
			{
				page += "<p>Are you sure you want to make this parcel an ERC721 NFT on the Ethereum blockchain?  This cannot currently be reversed.</p>";

				page += "<p>If you make this parcel an NFT, then the Substrata server will consider the owner of the parcel NFT to be the owner "
					" of the parcel.</p>";

				page += "<p>Ownership of the NFT will be assigned to your Ethereum address: <span class=\"eth-address\">" +
					logged_in_user->controlled_eth_address + "</span></p>";

				page +=
					"	<form action=\"/make_parcel_into_nft_post\" method=\"post\">																\n"
					"		<input type=\"hidden\" name=\"parcel_id\" value=\"" + parcel_id.toString() + "\"  />			\n"
					"		<button type=\"submit\" id=\"button-make-parcel-nft\" class=\"button-link\">Make parcel into NFT</button>			\n"
					"	</form>";
			}
		}
		else
		{
			page += "Parcel is already a NFT, or is currently being minted as a NFT.";
		}

		page += "</div>   \n"; // End main div
	}

	page += WebServerResponseUtils::standardFooter(request, /*include_email_link=*/true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void renderParcelClaimSucceeded(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	std::string page;
	page += WebServerResponseUtils::standardHeader(world_state, request, /*page title=*/"Successfully claimed ownership of parcel");

	page += "<div class=\"main\">   \n";
	// page += "<p>TEMP NO OWNERSHIP CHANGE DURING TESTING.  NO OWNERSHIP CHANGE WILL ACTUALLY TAKE PLACE.</p>";
	page += "<p>You have successfully claimed ownership of a parcel.  The parcel will now be listed on your <a href=\"/account\">account page</a>.</p>";
	page += "</div>   \n"; // End main div

	page += WebServerResponseUtils::standardFooter(request, /*include_email_link=*/true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void renderParcelClaimFailed(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	std::string page;
	page += WebServerResponseUtils::standardHeader(world_state, request, /*page title=*/"Ownership claim of parcel failed.");

	page += "<div class=\"main\">   \n";
	page += "<p>Sorry, the ownership claim for the parcel failed.</p>";

	page += "<p>This is either because the Ethereum address associated with your Substrata account is not the owner of the parcel NFT, the parcel was not minted as an NFT, or because there was an internal error in the query of the Ethereum block chain.</p>";
	
	page += "<p>Please visit the Discord channel (see homepage) or email contact@glaretechnologies.com for more info.</p>";
	
	page += "<p>You can try again via your <a href=\"/account\">account page</a>.</p>";
	page += "</div>   \n"; // End main div

	page += WebServerResponseUtils::standardFooter(request, /*include_email_link=*/true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void renderParcelClaimInvalid(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	std::string page;
	page += WebServerResponseUtils::standardHeader(world_state, request, /*page title=*/"Ownership claim of parcel was invalid");

	page += "<div class=\"main\">   \n";
	page += "<p>Sorry, the ownership claim for the parcel was not valid.</p>";

	page += "<p>This is either because you do not have an Ethereum address associated with your Substrata account, or you already own the parcel, or the parcel does not exist.</p>";
	
	page += "<p>Please visit the Discord channel (see homepage) or email contact@glaretechnologies.com for more info.</p>";
	
	page += "</div>   \n"; // End main div

	page += WebServerResponseUtils::standardFooter(request, /*include_email_link=*/true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void renderMakingParcelIntoNFT(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	const int parcel_id = request.getURLIntParam("parcel_id");

	std::string page;
	page += WebServerResponseUtils::standardHeader(world_state, request, /*page title=*/"Making parcel NFT");

	page += "<div class=\"main\">   \n";
	page += "<p>The transaction to mint your parcel as an NFT is queued.</p>";

	page += "<p>This may take a while to complete (Up to 24 hours).</p>";

	page += "<p>Once the transaction is complete, the minting transaction will be shown on the <a href=\"/parcel/" + toString(parcel_id) + "\">parcel page</a>.</p>";

	page += "<p>If the transaction does not complete in that time, please visit the Discord channel (see homepage) or email contact@glaretechnologies.com for more info.</p>";

	page += "</div>   \n"; // End main div

	page += WebServerResponseUtils::standardFooter(request, /*include_email_link=*/true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void renderMakingParcelIntoNFTFailed(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	std::string page;
	page += WebServerResponseUtils::standardHeader(world_state, request, /*page title=*/"Making parcel NFT failed.");

	page += "<div class=\"main\">   \n";
	page += "<p>Sorry, we failed to make the parcel into an NFT.</p>";

	page += "<p>Please visit the Discord channel (see homepage) or email contact@glaretechnologies.com for more info.</p>";

	page += "<p>You can try again via your <a href=\"/account\">account page</a>.</p>";
	page += "</div>   \n"; // End main div

	page += WebServerResponseUtils::standardFooter(request, /*include_email_link=*/true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void handleMakeParcelIntoNFTPost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	try
	{ // lock scope

		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		const ParcelID parcel_id(request_info.getPostIntField("parcel_id"));

		WorldStateLock lock(world_state.mutex);

		User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request_info);
		if(logged_in_user == NULL)
		{
			// page += WebServerResponseUtils::standardHTMLHeader(request, "User Account");
			// page += "You must be logged in to view your user account page.";
			web::ResponseUtils::writeRedirectTo(reply_info, "/account");
			return;
		}

		if(logged_in_user->controlled_eth_address.empty())
			throw glare::Exception("controlled eth address must be valid.");

		// Lookup parcel
		auto res = world_state.getRootWorldState()->parcels.find(parcel_id);
		if(res == world_state.getRootWorldState()->parcels.end())
			throw glare::Exception("No such parcel");

		Parcel* parcel = res->second.ptr();

		if(parcel->owner_id != logged_in_user->id)
			throw glare::Exception("Parcel must be owned by user");

		if(parcel->nft_status != Parcel::NFTStatus_NotNFT)
			throw glare::Exception("Parcel must not already be a NFT");


		// Transition the parcel into 'minting' state
		parcel->nft_status = Parcel::NFTStatus_MintingNFT;


		// Make an Eth transaction to mint the parcel
		SubEthTransactionRef transaction = new SubEthTransaction();
		transaction->id = world_state.getNextSubEthTransactionUID();
		transaction->created_time = TimeStamp::currentTime();
		transaction->state = SubEthTransaction::State_New;
		transaction->initiating_user_id = logged_in_user->id;
		transaction->parcel_id = parcel->id;
		transaction->user_eth_address = logged_in_user->controlled_eth_address;
		world_state.addSubEthTransactionAsDBDirty(transaction);

		parcel->minting_transaction_id = transaction->id;
		world_state.getRootWorldState()->addParcelAsDBDirty(parcel, lock);

		world_state.sub_eth_transactions[transaction->id] = transaction;

		world_state.markAsChanged();
	
	} // End lock scope
	catch(web::WebsiteExcep&)
	{
		web::ResponseUtils::writeRedirectTo(reply_info, "/making_parcel_into_nft_failed");
		return;
	}
	catch(glare::Exception&)
	{
		web::ResponseUtils::writeRedirectTo(reply_info, "/making_parcel_into_nft_failed");
		return;
	}

	web::ResponseUtils::writeRedirectTo(reply_info, "/making_parcel_into_nft?parcel_id=" + toString(request_info.getPostIntField("parcel_id")));
	return;
}


void handleClaimParcelOwnerByNFTPost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	ParcelID parcel_id;
	std::string user_controlled_eth_address;

	// Check user is logged in, check they have an eth address, check parcel exists.
	try
	{ // lock scope

		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		parcel_id = ParcelID(request_info.getPostIntField("parcel_id"));

		Lock lock(world_state.mutex);

		User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request_info);
		if(logged_in_user == NULL)
			throw glare::Exception("You must be logged in.");

		if(logged_in_user->controlled_eth_address.empty())
			throw glare::Exception("controlled eth address must be valid.");

		user_controlled_eth_address = logged_in_user->controlled_eth_address;

		// Lookup parcel
		auto res = world_state.getRootWorldState()->parcels.find(parcel_id);
		if(res == world_state.getRootWorldState()->parcels.end())
			throw glare::Exception("No such parcel");

		Parcel* parcel = res->second.ptr();

		if(parcel->owner_id == logged_in_user->id)
			throw glare::Exception("parcel already owned by user.");

	} // End lock scope
	catch(web::WebsiteExcep&)
	{
		web::ResponseUtils::writeRedirectTo(reply_info, "/parcel_claim_invalid");
		return;
	}
	catch(glare::Exception&)
	{
		web::ResponseUtils::writeRedirectTo(reply_info, "/parcel_claim_invalid");
		return;
	}

	// Don't do an Infura API call if fuzzing.
	if(request_info.fuzzing)
		return;

	// Do infura lookup
	bool succeeded = false;
	try
	{
		InfuraCredentials infura_credentials;
		infura_credentials.infura_project_id		= world_state.getCredential("infura_project_id");
		infura_credentials.infura_project_secret	= world_state.getCredential("infura_project_secret");

		const std::string network = "mainnet";
		const EthAddress substrata_smart_contact_addr = EthAddress::parseFromHexString("0xa4535F84e8D746462F9774319E75B25Bc151ba1D"); // This should be address of the Substrata parcel smart contract

		const EthAddress eth_parcel_owner = Infura::getOwnerOfERC721Token(infura_credentials, network, substrata_smart_contact_addr, UInt256(parcel_id.value()));

		if(eth_parcel_owner == EthAddress::parseFromHexString(user_controlled_eth_address))
		{
			// The logged in user does indeed own the parcel NFT.  So assign ownership of the parcel.

			{ // lock scope
				WorldStateLock lock(world_state.mutex);

				User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request_info);
				if(logged_in_user == NULL)
					throw glare::Exception("logged_in_user == NULL.");

				// Lookup parcel
				auto res = world_state.getRootWorldState()->parcels.find(parcel_id);
				if(res == world_state.getRootWorldState()->parcels.end())
					throw glare::Exception("No such parcel");

				Parcel* parcel = res->second.ptr();

				parcel->owner_id = logged_in_user->id;

				// Set parcel admins and writers to the new user as well.
				parcel->admin_ids  = std::vector<UserID>(1, UserID(logged_in_user->id));
				parcel->writer_ids = std::vector<UserID>(1, UserID(logged_in_user->id));
				
				world_state.getRootWorldState()->addParcelAsDBDirty(parcel, lock);

				// TODO: Log ownership change?

				world_state.denormaliseData();
				world_state.markAsChanged();

				succeeded = true;

			} // End lock scope
		}
	}
	catch(glare::Exception& e)
	{
		conPrint("Error while executing Infura::getOwnerOfERC721Token(): " + e.what());
	}

	if(succeeded)
		web::ResponseUtils::writeRedirectTo(reply_info, "/parcel_claim_succeeded");
	else
		web::ResponseUtils::writeRedirectTo(reply_info, "/parcel_claim_failed");
}


void renderScriptLog(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	std::string page;

	// Get a reference to the script log for this user.
	Reference<UserScriptLog> log;

	{ // lock scope
		Lock lock(world_state.mutex);

		User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
		if(logged_in_user == NULL)
		{
			page += WebServerResponseUtils::standardHeader(world_state, request, /*page title=*/"Script log");
			page += "You must be logged in to view this page.";
			page += WebServerResponseUtils::standardFooter(request, /*include_email_link=*/true);
			web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
			return;
		}

		page += WebServerResponseUtils::standardHeader(world_state, request, /*page title=*/"Script log");
		page += "<div class=\"main\">   \n";

		page += "<p>Showing output and error messages for all scripts created by <a href=\"/account\">" + web::Escaping::HTMLEscape(logged_in_user->name) + "</a>.</p>";

		auto res = world_state.user_script_log.find(logged_in_user->id);
		if(res != world_state.user_script_log.end())
			log = res->second;
	}

	

	// Do the conversion of the logs into the HTML without holding the main world_state mutex, since this could take a little while.
	if(log.nonNull())
	{
		Lock lock(log->mutex);

		page += "<pre class=\"script-log\">";

		for(auto it = log->messages.beginIt(); it != log->messages.endIt(); ++it)
		{
			const UserScriptLogMessage& msg = *it;

			page += "<span class=\"log-timestamp\">" + msg.time.RFC822FormatedString() + "</span>: <span class=\"log-ob\">ob " + msg.script_ob_uid.toString() + "</span>: ";
			if(msg.msg_type == UserScriptLogMessage::MessageType_error)
				page += "<span class=\"log-error\">";
			else
				page += "<span class=\"log-print\">";
			page += web::Escaping::HTMLEscape(msg.msg);
			page += "</span>\n";
		}

		page += "</pre>";
	}
	else
	{
		page += "[No messages]";
	}

	page += WebServerResponseUtils::standardFooter(request, /*include_email_link=*/true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void renderSecretsPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	std::string page;


	{ // lock scope
		Lock lock(world_state.mutex);

		User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
		if(logged_in_user == NULL)
		{
			page += WebServerResponseUtils::standardHeader(world_state, request, /*page title=*/"Secrets");
			page += "You must be logged in to view this page.";
			page += WebServerResponseUtils::standardFooter(request, /*include_email_link=*/true);
			web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
			return;
		}

		page += WebServerResponseUtils::standardHeader(world_state, request, /*page title=*/"Secrets");
		page += "<div class=\"main\">   \n";

		// Display any messages for the user
		const std::string msg_for_user = world_state.getAndRemoveUserWebMessage(logged_in_user->id);
		if(!msg_for_user.empty())
			page += "<div class=\"msg\">" + web::Escaping::HTMLEscape(msg_for_user) + "</div>  \n";

		page += "<p>Showing secrets that are accessible from Lua scripts.  Use for storing API keys etc.</p>";

		page += "<p>Note: the server administrator can see these values - only store information here that you are happy with the server administrator seeing.</p>";

		int num_secrets_displayed = 0;
		for(auto it = world_state.user_secrets.begin(); it != world_state.user_secrets.end(); ++it)
		{
			const UserSecret* secret = it->second.ptr();
			if(secret->key.user_id == logged_in_user->id)
			{
				page += "<p>" + web::Escaping::HTMLEscape(secret->key.secret_name) + ": " + web::Escaping::HTMLEscape(secret->value) + "</p>";
				num_secrets_displayed++;

				page +=
				"	<form action=\"/delete_secret_post\" method=\"post\">									\n"
				"		<input id=\"secret_name-id\" type=\"hidden\" name=\"secret_name\" value=\"" + web::Escaping::HTMLEscape(secret->key.secret_name) + "\" />			\n"
				"		<button type=\"submit\" id=\"delete-secret-button\" class=\"button-link\">Delete Secret</button>			\n"
				"	</form>																					\n"
				"	<br/>";
			}
		}

		if(num_secrets_displayed == 0)
			page += "[No secrets]";

		page +=
			"	<br/><br/>		\n"
			"	<form action=\"/add_secret_post\" method=\"post\">									\n"
			"		<label for=\"secret_name-id\">Secret name:</label><br/>							\n"
			"		<input id=\"secret_name-id\" type=\"string\" name=\"secret_name\" /><br/>			\n"

			"		<label for=\"secret_value-id\">Secret value:</label><br/>						\n"
			"		<input id=\"secret_value-id\" type=\"string\" name=\"secret_value\" /><br>			\n"

			"		<button type=\"submit\" id=\"add-secret-button\" class=\"button-link\">Add Secret</button>			\n"
			"	</form>";
	}

	page += WebServerResponseUtils::standardFooter(request, /*include_email_link=*/true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void handleAddSecretPost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	try
	{ // lock scope

		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		const web::UnsafeString secret_name  = request_info.getPostField("secret_name");
		const web::UnsafeString secret_value = request_info.getPostField("secret_value");

		WorldStateLock lock(world_state.mutex);

		User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request_info);
		if(logged_in_user == NULL)
		{
			// page += WebServerResponseUtils::standardHTMLHeader(request, "User Account");
			// page += "You must be logged in to view your user account page.";
			web::ResponseUtils::writeRedirectTo(reply_info, "/account");
			return;
		}

		UserSecretRef secret = new UserSecret();
		secret->key = UserSecretKey(/*user_id=*/logged_in_user->id, /*secret_name=*/secret_name.str());
		secret->value = secret_value.str();

		if(world_state.user_secrets.count(secret->key) > 0)
		{
			world_state.setUserWebMessage(logged_in_user->id, "Secret already exists with that name");
			web::ResponseUtils::writeRedirectTo(reply_info, "/secrets");
			return;
		}

		if(secret_name.str().size() > UserSecret::MAX_SECRET_NAME_SIZE)
		{
			world_state.setUserWebMessage(logged_in_user->id, "Secret name too long");
			web::ResponseUtils::writeRedirectTo(reply_info, "/secrets");
			return;
		}

		if(secret_value.str().size() > UserSecret::MAX_VALUE_SIZE)
		{
			world_state.setUserWebMessage(logged_in_user->id, "Secret value too long");
			web::ResponseUtils::writeRedirectTo(reply_info, "/secrets");
			return;
		}
		
		world_state.user_secrets.insert(std::make_pair(secret->key, secret));
		world_state.db_dirty_user_secrets.insert(secret);

		world_state.markAsChanged();

		world_state.setUserWebMessage(logged_in_user->id, "Secret added.");
	
	} // End lock scope
	catch(web::WebsiteExcep&)
	{
		web::ResponseUtils::writeRedirectTo(reply_info, "/secrets");
		return;
	}
	catch(glare::Exception&)
	{
		web::ResponseUtils::writeRedirectTo(reply_info, "/secrets");
		return;
	}

	web::ResponseUtils::writeRedirectTo(reply_info, "/secrets");
	return;
}


void handleDeleteSecretPost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	try
	{ // lock scope

		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		const web::UnsafeString secret_name  = request_info.getPostField("secret_name");

		WorldStateLock lock(world_state.mutex);

		User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request_info);
		if(logged_in_user == NULL)
		{
			// page += WebServerResponseUtils::standardHTMLHeader(request, "User Account");
			// page += "You must be logged in to view your user account page.";
			web::ResponseUtils::writeRedirectTo(reply_info, "/account");
			return;
		}

		const UserSecretKey key(/*user_id = */logged_in_user->id, secret_name.str());

		auto res = world_state.user_secrets.find(key);
		if(res == world_state.user_secrets.end())
			throw glare::Exception("No secret with given secret name exists.");
		

		UserSecretRef secret = res->second;
		
		world_state.db_dirty_user_secrets.erase(secret); // Remove from dirty-set, so it's not updated in DB.

		world_state.db_records_to_delete.insert(secret->database_key);

		world_state.user_secrets.erase(key);

		world_state.markAsChanged();

		world_state.setUserWebMessage(logged_in_user->id, "Secret deleted.");
	
	} // End lock scope
	catch(web::WebsiteExcep&)
	{
		web::ResponseUtils::writeRedirectTo(reply_info, "/secrets");
		return;
	}
	catch(glare::Exception&)
	{
		web::ResponseUtils::writeRedirectTo(reply_info, "/secrets");
		return;
	}

	web::ResponseUtils::writeRedirectTo(reply_info, "/secrets");
	return;
}


} // end namespace AccountHandlers


#if BUILD_TESTS


#include "../utils/TestUtils.h"


void AccountHandlers::test()
{
}


#endif // BUILD_TESTS
