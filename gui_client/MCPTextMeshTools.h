#pragma once

// Qt-local adapter. Do not include from the SDL/common client boundary.
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <string>
class GUIClient;
class WorldObject;

namespace MCPTextMeshTools
{
bool handles(const QString& name);
QJsonArray schemas();
bool isConvertedTextMeshURL(const std::string& model_url);
std::string convertedTextMeshFontName(const std::string& model_url);
// Rebuilds the mesh from the object's retained text using its updated text_font.
void rebuildConvertedTextMeshForFont(GUIClient& client, WorldObject& object);
// Must run on the GUI thread; uploads a native mesh and queues the normal,
// server-authorised object update. The returned status is not server acknowledgement.
QJsonObject handle(GUIClient& client, const QString& name, const QJsonObject& args);
}
