/*=====================================================================
MCPClientHandler.cpp
--------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "MCPClientHandler.h"
#include "MCPTextureTools.h"
#include "MCPImageGeneration.h"


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
	"{\"name\":\"inspect_terrain\",\"description\":\"Inspect the active world's terrain sections before editing. Returns section coordinates and bounds, whether each heightmap is currently loaded in the client, heightmap and mask layer presence, section width, base height, height scale and water level. The heightmap is the EXR layer that controls terrain shape; masks and detail textures are separate and are preserved when smoothing terrain. If the target section is not loaded, move the client camera near it and inspect again.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}}";
const char* SMOOTH_TERRAIN_TOOL_JSON =
	"{\"name\":\"smooth_terrain\",\"description\":\"Smooth terrain heights in a circular world-space region by editing only the active section heightmap EXR layer. Use inspect_terrain first and target sections where heightmap_loaded=true. A brush that crosses an outer/unloaded section edge edits its loaded portion and skips missing sections instead of failing the whole operation. For a long straight shoreline or section edge, place several overlapping circular brushes along the visible edge so the full requested stretch is smoothed, with a soft transition inland. Derive world x/y from the screenshot and render_view; do not assume the camera position is the edit center. Strength is 0.05..1; passes is 1..8. The result reports edited and skipped section counts, is saved as new EXR resources and sent through the normal world-settings update. It preserves tree, road, building, terrain masks and detail textures. Requires owner/god write access.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"center\":{\"type\":\"object\",\"properties\":{\"x\":{\"type\":\"number\"},\"y\":{\"type\":\"number\"}},\"required\":[\"x\",\"y\"],\"additionalProperties\":false},\"radius_m\":{\"type\":\"number\",\"minimum\":1},\"strength\":{\"type\":\"number\",\"minimum\":0.01,\"maximum\":1},\"passes\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":8}},\"required\":[\"center\",\"radius_m\",\"strength\"],\"additionalProperties\":false}}";
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


std::string spliceRenderViewIntoToolsList(const std::string& response)
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
	for(const QJsonValue& tool : MCPTextures::schemas()) tools.append(tool);
	for(const QJsonValue& tool : MCPImages::schemas()) tools.append(tool);
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


MCPClientRequestHandler::MCPClientRequestHandler(const MCPClientForwardingTarget& forwarding_target, const MCPClientRenderCallback& render_callback_, const MCPClientStatusCallback& status_callback_, const std::function<bool()>& poly_haven_enabled_, const MCPClientTerrainCallback& terrain_callback_)
:	server_hostname(forwarding_target.server_hostname),
	render_callback(render_callback_),
	terrain_callback(terrain_callback_),
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

		if(method == "tools/call" && root.hasChild("params"))
		{
			const JSONNode& params = root.getChildObject(parser, "params");
			const QString name=QString::fromStdString(params.getChildStringValue(parser,"name"));
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
				if(tool_name == "inspect_terrain" || tool_name == "smooth_terrain")
				{
					if(!terrain_callback)
						throw glare::Exception("World terrain tools are not available in this client build.");
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
		std::string response = forwardToServer(body);
			if(method == "tools/list" && (render_callback || terrain_callback))
				response = spliceRenderViewIntoToolsList(response);
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
	return new MCPClientRequestHandler(forwarding_target, render_callback, status_callback, poly_haven_enabled, terrain_callback);
}
