#pragma once

#include <QtCore/QList>
#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <functional>


struct PluginCommandRegistration
{
	QString pluginId;
	QString pluginVersion;
	QString commandId;
	QString title;
	std::function<void()> callback;
};


struct PluginPanelRegistration
{
	QString pluginId;
	QString pluginVersion;
	QString viewId;
	QString title;
	QJsonObject layout;
	std::function<void(const QString&)> actionCallback;
};


// Stable, capability-limited host boundary shared by Luau and future WASM
// adapters. Plugin bytecode sees only bounded functions, never this object.
class PluginHostInterface
{
public:
	virtual ~PluginHostInterface() {}
	virtual bool registerCommand(const QString& pluginId, const QString& pluginVersion,
		const QString& commandId, const QString& title,
		const std::function<void()>& callback,
		QString& error) = 0;
	virtual bool registerPanel(const QString& pluginId, const QString& pluginVersion,
		const QString& viewId, const QString& title, const QJsonObject& layout,
		const std::function<void(const QString&)>& actionCallback, QString& error) = 0;
	virtual void removePluginResources(const QString& pluginId, const QString& pluginVersion) = 0;
	virtual void closePanel(const QString& pluginId, const QString& pluginVersion, const QString& viewId) = 0;
	virtual void showNotification(const QString& pluginId, const QString& title, const QString& body) = 0;
	// Returns a bounded, read-only snapshot. Implementations must run this on the
	// UI thread and must not expose internal engine objects to the plugin.
	virtual bool getSceneSummary(const QString& pluginId, const QString& pluginVersion,
		QJsonObject& summary, QString& error)
	{
		Q_UNUSED(pluginId);
		Q_UNUSED(pluginVersion);
		summary = QJsonObject();
		error = QStringLiteral("The scene summary capability is unavailable in this host.");
		return false;
	}
	virtual bool isUiThread() const = 0;
	virtual QList<PluginCommandRegistration> registeredCommands() const = 0;
	virtual QList<PluginPanelRegistration> registeredPanels() const = 0;
};


bool validatePluginPanelLayout(const QJsonObject& layout, QString& error);
