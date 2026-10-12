#pragma once

// Qt-local adapter. Do not include from the SDL/common client boundary.
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QString>
class GUIClient;

namespace MCPTextMeshTools
{
bool handles(const QString& name);
QJsonArray schemas();
// Must run on the GUI thread; uploads a native mesh and queues the normal,
// server-authorised object update. The returned status is not server acknowledgement.
QJsonObject handle(GUIClient& client, const QString& name, const QJsonObject& args);
}
