#pragma once
#if !defined(USE_SDL)
#include <functional>
#include <QtCore/QString>
class GUIClient;
class WorldObject;
class QObject;

namespace ObjectGLBExport {
// Call on the GUI thread. Captures a locked snapshot, requests missing originals,
// then exports on a worker. Throws glare::Exception for preflight failures.
// Completion runs on callbackContext's thread; empty error means committed.
// Destroying callbackContext suppresses the callback without invalidating the worker.
void start(GUIClient& client,const WorldObject& object,const QString& output_path,
	QObject* callback_context,std::function<void(const QString& error)> completion);
}
#endif
