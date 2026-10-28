#include "MCPClientLocalOnlyProtocol.h"

#include <QtCore/QJsonDocument>

namespace
{
QByteArray encodeResponse(const QJsonValue& id, const QJsonObject& result)
{
	return QJsonDocument(QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}}).toJson(QJsonDocument::Compact);
}

QByteArray encodeError(const QJsonValue& id, int code, const QString& message)
{
	return QJsonDocument(QJsonObject{{"jsonrpc", "2.0"}, {"id", id},
		{"error", QJsonObject{{"code", code}, {"message", message}}}}).toJson(QJsonDocument::Compact);
}
}


bool MCPClientLocalOnlyProtocol::handlesMethod(const QString& method)
{
	return method == QStringLiteral("initialize") || method == QStringLiteral("ping") ||
		method == QStringLiteral("tools/list") || method == QStringLiteral("resources/list") ||
		method == QStringLiteral("resources/templates/list") || method == QStringLiteral("prompts/list") ||
		method == QStringLiteral("tools/call");
}


QByteArray MCPClientLocalOnlyProtocol::makeResponse(const QString& method, const QJsonValue& id,
	const QJsonObject& params, const QJsonArray& blender_tools, const QString& client_version)
{
	if(method == QStringLiteral("initialize"))
	{
		QString protocol_version = params.value(QStringLiteral("protocolVersion")).toString();
		if(protocol_version != QStringLiteral("2025-03-26") && protocol_version != QStringLiteral("2024-11-05"))
			protocol_version = QStringLiteral("2025-03-26");
		const QJsonObject result{
			{"protocolVersion", protocol_version},
			{"capabilities", QJsonObject{
				{"tools", QJsonObject{{"listChanged", false}}},
				{"resources", QJsonObject{{"listChanged", false}, {"subscribe", false}}}
			}},
			{"serverInfo", QJsonObject{{"name", "Metasiberia Local MCP"}, {"version", client_version}}},
			{"instructions", "Local-only session: Blender tools operate on the explicitly paired local Blender instance. No remote Metasiberia world tools are configured, so world tools and direct placement are unavailable in this session. Blender scene changes remain local unless the user explicitly requests export and a world connection is available."}
		};
		return encodeResponse(id, result);
	}
	if(method == QStringLiteral("ping"))
		return encodeResponse(id, QJsonObject());
	if(method == QStringLiteral("tools/list"))
		return encodeResponse(id, QJsonObject{{"tools", blender_tools}});
	if(method == QStringLiteral("resources/list"))
		return encodeResponse(id, QJsonObject{{"resources", QJsonArray()}});
	if(method == QStringLiteral("resources/templates/list"))
		return encodeResponse(id, QJsonObject{{"resourceTemplates", QJsonArray()}});
	if(method == QStringLiteral("prompts/list"))
		return encodeResponse(id, QJsonObject{{"prompts", QJsonArray()}});
	if(method == QStringLiteral("tools/call"))
	{
		const QString tool_name = params.value(QStringLiteral("name")).toString();
		const QString message = QStringLiteral("World MCP tool '%1' is unavailable because no Metasiberia world is connected. Blender tools remain available in this local-only session.").arg(tool_name);
		const QJsonObject error{{"code", "WORLD_NOT_CONNECTED"}, {"message", message}};
		return encodeResponse(id, QJsonObject{
			{"isError", true},
			{"structuredContent", QJsonObject{{"error", error}}},
			{"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", message}}}}
		});
	}
	return encodeError(id, -32601, QStringLiteral("Method not found in the local-only Metasiberia MCP session."));
}
