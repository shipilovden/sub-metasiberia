/*=====================================================================
MCPClientHandler.cpp
--------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "MCPClientHandler.h"


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


namespace
{

const size_t MAX_MCP_REQUEST_BODY_SIZE = 1024 * 1024;


// Kept in sync with the current upstream render_view contract.
const char* RENDER_VIEW_TOOL_JSON =
	"{"
		"\"name\":\"render_view\","
		"\"description\":\"Render an image of the currently-connected world from a given camera, and return it as an image. "
			"Use this to see what the world looks like, e.g. to check what you have built. Rendering moves the streaming camera "
			"and waits for the world to finish loading around it, so this can take a few seconds.\","
		"\"inputSchema\":{\"type\":\"object\",\"properties\":{"
			"\"cam_pos\":{\"type\":\"object\",\"description\":\"Camera position as {x,y,z} in metres (z is up).\"},"
			"\"cam_angles\":{\"type\":\"object\",\"description\":\"Camera orientation as {heading,pitch,roll} in radians. heading rotates in the x-y plane from +x towards +y (0 looks along +x, pi/2 looks along +y). pitch is a POLAR angle from the +z (up) axis: 0 looks straight up, pi/2 (~1.571) is level/horizontal, pi (~3.14) looks straight down. roll is usually 0.\"},"
			"\"width\":{\"type\":\"number\",\"description\":\"Image width in pixels (default 1024).\"},"
			"\"height\":{\"type\":\"number\",\"description\":\"Image height in pixels (default 768).\"}"
		"},\"required\":[\"cam_pos\",\"cam_angles\"]}"
	"}";


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
	const std::string marker = "\"tools\":[";
	const size_t pos = response.find(marker);
	if(pos == std::string::npos)
		return response;

	const size_t insert_pos = pos + marker.size();
	size_t k = insert_pos;
	while(k < response.size() && isWhitespace(response[k]))
		k++;
	const bool empty_array = (k < response.size()) && response[k] == ']';

	std::string insertion(RENDER_VIEW_TOOL_JSON);
	if(!empty_array)
		insertion += ",";
	return response.substr(0, insert_pos) + insertion + response.substr(insert_pos);
}


bool isFiniteVec(const Vec3d& v)
{
	return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

} // anonymous namespace


MCPClientRequestHandler::MCPClientRequestHandler(const MCPClientForwardingTarget& forwarding_target, const MCPClientRenderCallback& render_callback_, const MCPClientStatusCallback& status_callback_)
:	server_hostname(forwarding_target.server_hostname),
	render_callback(render_callback_),
	status_callback(status_callback_),
	keepalive_configured(false)
{
	http_client = new HTTPClient();
	http_client->max_data_size = 16 * 1024 * 1024;

	if(!forwarding_target.username.empty() && !forwarding_target.password.empty())
	{
		http_client->additional_headers.push_back("Authorization: Substrata-Login " +
			StringUtils::convertByteArrayToHexString((const unsigned char*)forwarding_target.username.data(), forwarding_target.username.size()) + "." +
			StringUtils::convertByteArrayToHexString((const unsigned char*)forwarding_target.password.data(), forwarding_target.password.size()));
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
			http_client->sendPost(server_mcp_url, request_body, /*content type=*/"application/json", response);
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
		request.width = (int)args.getChildDoubleValueWithDefaultVal(parser, "width", /*default=*/1024);
		request.height = (int)args.getChildDoubleValueWithDefaultVal(parser, "height", /*default=*/768);

		if(!isFiniteVec(request.cam_pos) || !isFiniteVec(request.cam_angles))
			throw glare::Exception("Camera position and angles must be finite.");
		if(request.width < 16 || request.width > 4096 || request.height < 16 || request.height > 4096)
			throw glare::Exception("width/height out of range [16, 4096].");

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

		if(method == "tools/call" && root.hasChild("params"))
		{
			const JSONNode& params = root.getChildObject(parser, "params");
			if(params.hasChild("name") && params.getChildStringValue(parser, "name") == "render_view")
			{
				handleRenderView(parser, root, reply_info);
				return;
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
		if(method == "tools/list" && render_callback)
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


bool MCPClientSharedRequestHandler::configureForwardingTargetFromCredentialManager(const std::string& server_hostname, CredentialManager& credential_manager)
{
	clearForwardingTarget();
	if(!isValidServerHostname(server_hostname))
		return false;

	const std::string username = credential_manager.getUsernameForDomain(server_hostname);
	const std::string password = credential_manager.getDecryptedPasswordForDomain(server_hostname);
	if(username.empty() || password.empty() || username.size() > 4096 || password.size() > 4096)
		return false;

	forwarding_target.server_hostname = server_hostname;
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
	return new MCPClientRequestHandler(forwarding_target, render_callback, status_callback);
}
