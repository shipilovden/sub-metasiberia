/*=====================================================================
MCPHandlers.h
-------------
Small JSON-RPC MCP endpoint used by the local Codex bridge.
=====================================================================*/
#pragma once

#include <string>
class ServerAllWorldsState;
class Server;
namespace web { class RequestInfo; class ReplyInfo; }

namespace MCPHandlers
{
void handleMCPRequest(Server& server, ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info);
}
