#pragma once

#include "PluginHostInterface.h"

#include <QtCore/QMap>
#include <QtCore/QHash>
#include <QtCore/QMutex>
#include <functional>
#include <memory>


// Qt-side adapter: owns only plugin command callbacks and routes notifications
// to an application-supplied UI-thread sink. It is not exposed to Luau code.
class QtPluginHostAdapter final : public PluginHostInterface
{
public:
	explicit QtPluginHostAdapter(std::function<void(const QString&, const QString&, const QString&)> notificationSink);
	~QtPluginHostAdapter() override;
	void setSceneSummaryProvider(std::function<bool(const QString&, const QString&, QJsonObject&, QString&)> provider);
	void setLifecycleCallbacks(std::function<void()> registrationsChanged,
		std::function<void(const QString&, const QString&, const QString&)> closeView);

	bool registerCommand(const QString& pluginId, const QString& pluginVersion,
		const QString& commandId, const QString& title,
		const std::function<void()>& callback,
		QString& error) override;
	bool registerPanel(const QString& pluginId, const QString& pluginVersion,
		const QString& viewId, const QString& title, const QJsonObject& layout,
		const std::function<void(const QString&)>& actionCallback, QString& error) override;
	void removePluginResources(const QString& pluginId, const QString& pluginVersion) override;
	void closePanel(const QString& pluginId, const QString& pluginVersion, const QString& viewId) override;
	void showNotification(const QString& pluginId, const QString& title, const QString& body) override;
	bool getSceneSummary(const QString& pluginId, const QString& pluginVersion,
		QJsonObject& summary, QString& error) override;
	bool isUiThread() const override;
	QList<PluginCommandRegistration> registeredCommands() const override;
	QList<PluginPanelRegistration> registeredPanels() const override;
	bool dispatchPanelAction(const QString& pluginId, const QString& pluginVersion,
		const QString& viewId, const QString& actionId);

private:
	static QString registrationKey(const QString& pluginId, const QString& pluginVersion, const QString& commandId);
	static QString panelKey(const QString& pluginId, const QString& pluginVersion, const QString& viewId);
	void notifyRegistrationsChanged();
	mutable QMutex registrations_mutex;
	QMap<QString, PluginCommandRegistration> command_registrations;
	QMap<QString, PluginPanelRegistration> panel_registrations;
	QHash<QString, quint64> plugin_generations;
	std::function<void(const QString&, const QString&, const QString&)> notification_sink;
	std::function<bool(const QString&, const QString&, QJsonObject&, QString&)> scene_summary_provider;
	std::function<void()> registrations_changed_sink;
	std::function<void(const QString&, const QString&, const QString&)> close_view_sink;
	std::shared_ptr<int> notification_lifetime = std::make_shared<int>(0);
};
