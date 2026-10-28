#include "QtPluginHostAdapter.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QMetaObject>
#include <QtCore/QMutexLocker>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QRegularExpression>
#include <QtCore/QSet>
#include <QtCore/QThread>

#include <cmath>


namespace
{
bool validatePanelNode(const QJsonObject& node, const int depth, int& node_count,
	QSet<QString>& action_ids, QString& error)
{
	if(depth > 4 || ++node_count > 64)
	{
		error = QStringLiteral("Panel layout exceeds its nesting or element limit.");
		return false;
	}
	const QString type = node.value(QStringLiteral("type")).toString();
	if(type == QStringLiteral("column"))
	{
		const QJsonArray children = node.value(QStringLiteral("children")).toArray();
		if(!node.value(QStringLiteral("children")).isArray() || children.isEmpty() || children.size() > 32)
		{
			error = QStringLiteral("A panel column must contain between 1 and 32 elements.");
			return false;
		}
		for(const QJsonValue& child : children)
			if(!child.isObject() || !validatePanelNode(child.toObject(), depth + 1, node_count, action_ids, error))
			{
				if(error.isEmpty()) error = QStringLiteral("Panel column children must be objects.");
				return false;
			}
		return true;
	}
	if(type == QStringLiteral("text"))
	{
		const QString text = node.value(QStringLiteral("text")).toString();
		const QString style = node.value(QStringLiteral("style")).toString(QStringLiteral("body"));
		if(text.isEmpty() || text.size() > 1000 ||
			(style != QStringLiteral("body") && style != QStringLiteral("heading") && style != QStringLiteral("muted")))
		{
			error = QStringLiteral("Panel text is empty, too long, or uses an unsupported style.");
			return false;
		}
		return true;
	}
	if(type == QStringLiteral("button"))
	{
		const QString id = node.value(QStringLiteral("id")).toString();
		const QString label = node.value(QStringLiteral("label")).toString().trimmed();
		static const QRegularExpression id_pattern(QStringLiteral("^[a-z][a-z0-9._-]{0,63}$"));
		if(!id_pattern.match(id).hasMatch() || label.isEmpty() || label.size() > 80 ||
			label.contains(QLatin1Char('\n')) || label.contains(QLatin1Char('\r')) || action_ids.contains(id))
		{
			error = QStringLiteral("Panel buttons require a unique safe id and a short single-line label.");
			return false;
		}
		action_ids.insert(id);
		return true;
	}
	error = QStringLiteral("Unsupported plugin panel element type.");
	return false;
}


bool isValidSceneSummaryDto(const QJsonObject& summary)
{
	if(summary.size() != 3 || !summary.value(QStringLiteral("connected")).isBool()) return false;
	const QJsonObject server = summary.value(QStringLiteral("server")).toObject();
	const QJsonObject camera = summary.value(QStringLiteral("camera")).toObject();
	if(server.size() != 2 || camera.size() != 3 ||
		!server.value(QStringLiteral("host")).isString() || server.value(QStringLiteral("host")).toString().size() > 253 ||
		!server.value(QStringLiteral("world")).isString() || server.value(QStringLiteral("world")).toString().size() > 128)
		return false;
	for(const QString& coordinate : {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z")})
		if(!camera.value(coordinate).isDouble() || !std::isfinite(camera.value(coordinate).toDouble())) return false;
	return QJsonDocument(summary).toJson(QJsonDocument::Compact).size() <= 4096;
}
}


bool validatePluginPanelLayout(const QJsonObject& layout, QString& error)
{
	error.clear();
	if(layout.value(QStringLiteral("type")).toString() != QStringLiteral("column"))
	{
		error = QStringLiteral("A plugin panel root must be a vertical column.");
		return false;
	}
	int node_count = 0;
	QSet<QString> action_ids;
	return validatePanelNode(layout, 0, node_count, action_ids, error);
}


QtPluginHostAdapter::QtPluginHostAdapter(
	std::function<void(const QString&, const QString&, const QString&)> notificationSink)
: notification_sink(std::move(notificationSink))
{}


QtPluginHostAdapter::~QtPluginHostAdapter()
{
	notification_lifetime.reset();
}


void QtPluginHostAdapter::setSceneSummaryProvider(
	std::function<bool(const QString&, const QString&, QJsonObject&, QString&)> provider)
{
	QMutexLocker lock(&registrations_mutex);
	scene_summary_provider = std::move(provider);
}


void QtPluginHostAdapter::setLifecycleCallbacks(std::function<void()> registrationsChanged,
	std::function<void(const QString&, const QString&, const QString&)> closeView)
{
	QMutexLocker lock(&registrations_mutex);
	registrations_changed_sink = std::move(registrationsChanged);
	close_view_sink = std::move(closeView);
}


QString QtPluginHostAdapter::registrationKey(const QString& pluginId, const QString& pluginVersion, const QString& commandId)
{
	return pluginId + QLatin1Char('@') + pluginVersion + QLatin1Char('/') + commandId;
}


bool QtPluginHostAdapter::registerCommand(const QString& pluginId, const QString& pluginVersion,
	const QString& commandId, const QString& title, const std::function<void()>& callback, QString& error)
{
	if(pluginId.isEmpty() || pluginVersion.isEmpty() || commandId.isEmpty() || title.isEmpty() || !callback)
	{
		error = QStringLiteral("Plugin command registration is incomplete.");
		return false;
	}
	{
		QMutexLocker lock(&registrations_mutex);
		if(command_registrations.size() + panel_registrations.size() >= 512)
		{
			error = QStringLiteral("The client plugin action limit has been reached.");
			return false;
		}
		for(const PluginPanelRegistration& panel : panel_registrations)
			if(panel.pluginId == pluginId && panel.pluginVersion == pluginVersion)
			{
				error = QStringLiteral("A plugin can register only one primary menu action.");
				return false;
			}
		for(const PluginCommandRegistration& command : command_registrations)
			if(command.pluginId == pluginId && command.pluginVersion == pluginVersion)
			{
				error = QStringLiteral("A plugin can register only one primary menu action.");
				return false;
			}
		const QString key = registrationKey(pluginId, pluginVersion, commandId);
		if(command_registrations.contains(key))
		{
			error = QStringLiteral("A command with this plugin-scoped id is already registered.");
			return false;
		}
		command_registrations.insert(key, PluginCommandRegistration{pluginId, pluginVersion, commandId, title, callback});
	}
	notifyRegistrationsChanged();
	return true;
}


QString QtPluginHostAdapter::panelKey(const QString& pluginId, const QString& pluginVersion, const QString& viewId)
{
	return pluginId + QLatin1Char('@') + pluginVersion + QLatin1Char('/') + viewId;
}


bool QtPluginHostAdapter::registerPanel(const QString& pluginId, const QString& pluginVersion,
	const QString& viewId, const QString& title, const QJsonObject& layout,
	const std::function<void(const QString&)>& actionCallback, QString& error)
{
	error.clear();
	static const QRegularExpression id_pattern(QStringLiteral("^[a-z][a-z0-9._-]{0,63}$"));
	if(pluginId.isEmpty() || pluginVersion.isEmpty() || !id_pattern.match(viewId).hasMatch() ||
		title.trimmed().isEmpty() || title.size() > 80 || !actionCallback)
	{
		error = QStringLiteral("Plugin panel registration is incomplete or invalid.");
		return false;
	}
	if(!validatePluginPanelLayout(layout, error))
		return false;
	{
		QMutexLocker lock(&registrations_mutex);
		if(command_registrations.size() + panel_registrations.size() >= 512)
		{
			error = QStringLiteral("The client plugin action limit has been reached.");
			return false;
		}
		for(const PluginCommandRegistration& command : command_registrations)
			if(command.pluginId == pluginId && command.pluginVersion == pluginVersion)
			{
				error = QStringLiteral("A plugin can register only one primary menu action.");
				return false;
			}
		for(const PluginPanelRegistration& panel : panel_registrations)
			if(panel.pluginId == pluginId && panel.pluginVersion == pluginVersion)
			{
				error = QStringLiteral("A plugin can register only one primary panel action.");
				return false;
			}
		const QString key = panelKey(pluginId, pluginVersion, viewId);
		if(panel_registrations.contains(key))
		{
			error = QStringLiteral("A plugin view with this plugin-scoped id is already registered.");
			return false;
		}
		panel_registrations.insert(key, PluginPanelRegistration{pluginId, pluginVersion, viewId, title, layout, actionCallback});
	}
	notifyRegistrationsChanged();
	return true;
}


void QtPluginHostAdapter::removePluginResources(const QString& pluginId, const QString& pluginVersion)
{
	QList<QString> closed_views;
	std::function<void(const QString&, const QString&, const QString&)> close_sink;
	bool changed = false;
	{
		QMutexLocker lock(&registrations_mutex);
		++plugin_generations[pluginId];
		for(auto it = command_registrations.begin(); it != command_registrations.end(); )
		{
			if(it->pluginId == pluginId && it->pluginVersion == pluginVersion)
			{
				it = command_registrations.erase(it);
				changed = true;
			}
			else ++it;
		}
		for(auto it = panel_registrations.begin(); it != panel_registrations.end(); )
		{
			if(it->pluginId == pluginId && it->pluginVersion == pluginVersion)
			{
				closed_views.push_back(it->viewId);
				it = panel_registrations.erase(it);
				changed = true;
			}
			else ++it;
		}
		close_sink = close_view_sink;
	}
	if(close_sink)
		for(const QString& view_id : closed_views) close_sink(pluginId, pluginVersion, view_id);
	if(changed) notifyRegistrationsChanged();
}


void QtPluginHostAdapter::closePanel(const QString& pluginId, const QString& pluginVersion, const QString& viewId)
{
	std::function<void(const QString&, const QString&, const QString&)> close_sink;
	{
		QMutexLocker lock(&registrations_mutex);
		if(!panel_registrations.contains(panelKey(pluginId, pluginVersion, viewId))) return;
		close_sink = close_view_sink;
	}
	if(close_sink) close_sink(pluginId, pluginVersion, viewId);
}


bool QtPluginHostAdapter::getSceneSummary(const QString& pluginId, const QString& pluginVersion,
	QJsonObject& summary, QString& error)
{
	if(!isUiThread())
	{
		error = QStringLiteral("Scene summaries are available only through the UI-thread host bridge.");
		return false;
	}
	std::function<bool(const QString&, const QString&, QJsonObject&, QString&)> provider;
	{
		QMutexLocker lock(&registrations_mutex);
		provider = scene_summary_provider;
	}
	if(!provider)
	{
		error = QStringLiteral("The client has no scene summary provider.");
		return false;
	}
	QJsonObject candidate;
	if(!provider(pluginId, pluginVersion, candidate, error)) return false;
	if(!isValidSceneSummaryDto(candidate))
	{
		error = QStringLiteral("The scene summary provider returned an invalid or oversized DTO.");
		return false;
	}
	summary = candidate;
	return true;
}


void QtPluginHostAdapter::notifyRegistrationsChanged()
{
	std::function<void()> callback;
	{
		QMutexLocker lock(&registrations_mutex);
		callback = registrations_changed_sink;
	}
	if(callback) callback();
}


bool QtPluginHostAdapter::dispatchPanelAction(const QString& pluginId, const QString& pluginVersion,
	const QString& viewId, const QString& actionId)
{
	std::function<void(const QString&)> callback;
	{
		QMutexLocker lock(&registrations_mutex);
		const auto it = panel_registrations.constFind(panelKey(pluginId, pluginVersion, viewId));
		if(it == panel_registrations.constEnd()) return false;
		callback = it->actionCallback;
	}
	if(!callback) return false;
	callback(actionId);
	return true;
}


void QtPluginHostAdapter::showNotification(const QString& pluginId, const QString& title, const QString& body)
{
	if(!notification_sink)
		return;
	if(isUiThread())
	{
		notification_sink(pluginId, title, body);
		return;
	}
	const auto sink = notification_sink;
	const std::weak_ptr<int> lifetime = notification_lifetime;
	quint64 generation = 0;
	{
		QMutexLocker lock(&registrations_mutex);
		generation = plugin_generations.value(pluginId, 0);
	}
	QCoreApplication* const application = QCoreApplication::instance();
	if(application)
		QMetaObject::invokeMethod(application, [this, lifetime, sink, pluginId, title, body, generation]() {
			if(lifetime.expired()) return;
			{
				QMutexLocker lock(&registrations_mutex);
				if(plugin_generations.value(pluginId, 0) != generation) return;
			}
			sink(pluginId, title, body);
		}, Qt::QueuedConnection);
}


bool QtPluginHostAdapter::isUiThread() const
{
	return QCoreApplication::instance() && QThread::currentThread() == QCoreApplication::instance()->thread();
}


QList<PluginCommandRegistration> QtPluginHostAdapter::registeredCommands() const
{
	QMutexLocker lock(&registrations_mutex);
	return command_registrations.values();
}


QList<PluginPanelRegistration> QtPluginHostAdapter::registeredPanels() const
{
	QMutexLocker lock(&registrations_mutex);
	return panel_registrations.values();
}
