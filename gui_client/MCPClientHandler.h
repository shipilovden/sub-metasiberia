/*=====================================================================
MCPClientHandler.h
------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include <webserver/RequestHandler.h>
#include <maths/vec3.h>
#include <utils/Reference.h>
#include <functional>
#include <string>
#include <vector>
class CredentialManager;
class HTTPClient;
class JSONParser;
struct JSONNode;


struct MCPClientRenderRequest
{
	Vec3d cam_pos;
	Vec3d cam_angles;
	int width;
	int height;
};


struct MCPClientRenderResult
{
	std::vector<unsigned char> encoded_image;
	std::string mime_type; // Currently expected to be image/jpeg or image/png.
};


typedef std::function<void (const MCPClientRenderRequest&, MCPClientRenderResult&)> MCPClientRenderCallback;
typedef std::function<void (const std::string&)> MCPClientStatusCallback;


struct MCPClientForwardingTarget
{
	std::string server_hostname;
	std::string username;
	std::string password;
};


/*=====================================================================
MCPClientRequestHandler
-----------------------
Serves the local MCP HTTP endpoint.  render_view is dispatched through a
callback supplied by the Qt integration; all other methods are forwarded to
the connected server's HTTPS /mcp endpoint using the login already stored by
CredentialManager.

This handler rejects every non-loopback peer.  The listener must ALSO bind to
127.0.0.1/::1.  Do not start it with the current wildcard-only
web::WebListenerThread API.
=====================================================================*/
class MCPClientRequestHandler : public web::RequestHandler
{
public:
	MCPClientRequestHandler(const MCPClientForwardingTarget& forwarding_target, const MCPClientRenderCallback& render_callback, const MCPClientStatusCallback& status_callback);

	virtual void handleRequest(const web::RequestInfo& request_info, web::ReplyInfo& reply_info) override;

private:
	void handleRenderView(const JSONParser& parser, const JSONNode& root, web::ReplyInfo& reply_info);
	std::string forwardToServer(const std::string& request_body); // Throws glare::Exception on failure.
	void reportStatus(const std::string& message) const;

	std::string server_hostname;
	MCPClientRenderCallback render_callback;
	MCPClientStatusCallback status_callback;

	// A request handler is created per incoming connection and used serially by
	// one worker thread, so HTTPClient needs no locking. Keep-alive lets
	// forwarded calls reuse the TLS connection.
	Reference<HTTPClient> http_client;
	bool keepalive_configured;
};


/*=====================================================================
MCPClientSharedRequestHandler
-----------------------------
Configure this object completely on the GUI thread before handing it to a
listener.  configureForwardingTargetFromCredentialManager() is the only
credential integration point: the MCP settings UI never reads, stores or
displays a password.
=====================================================================*/
class MCPClientSharedRequestHandler : public web::SharedRequestHandler
{
public:
	MCPClientSharedRequestHandler();
	~MCPClientSharedRequestHandler();

	// Takes a runtime snapshot of the saved login for server_hostname. Returns
	// false when no complete saved login exists or the hostname is invalid.
	bool configureForwardingTargetFromCredentialManager(const std::string& server_hostname, CredentialManager& credential_manager);
	void clearForwardingTarget();

	void setRenderCallback(const MCPClientRenderCallback& callback) { render_callback = callback; }
	void setStatusCallback(const MCPClientStatusCallback& callback) { status_callback = callback; }

	bool isConfigured() const;

	virtual Reference<web::RequestHandler> getOrMakeRequestHandler() override;

private:
	MCPClientForwardingTarget forwarding_target;
	MCPClientRenderCallback render_callback;
	MCPClientStatusCallback status_callback;
};
