#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtCore/QString>

namespace MCPClientLocalOnlyProtocol
{
	// Returns true when the request can be answered without a connected world.
	bool handlesMethod(const QString& method);

	// Builds a JSON-RPC response for the client-only MCP mode. The caller routes
	// Blender tools before using this fallback; non-Blender tools are reported as
	// unavailable instead of being forwarded to a missing remote server.
	QByteArray makeResponse(const QString& method, const QJsonValue& id, const QJsonObject& params,
		const QJsonArray& blender_tools, const QString& client_version);
}
