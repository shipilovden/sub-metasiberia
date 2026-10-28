/*=====================================================================
MCPBlenderBridge.h
------------------
Typed local bridge from the Metasiberia MCP endpoint to Blender MCP.
=====================================================================*/
#pragma once

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QString>

namespace MCPBlenderBridge
{
	QJsonArray toolSchemas();
	QJsonObject handleTool(const QString& name, const QJsonObject& arguments);
	QString lastExportedModelPath();
	bool isLastExportedModelPath(const QString& path);
	void clearSession();
	void writeDiscoveryFile(int mcp_port);
	void removeDiscoveryFile();
	void setAIActivity(bool active, const QString& phase, const QString& current_task);
}
