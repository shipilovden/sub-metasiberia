#include "GUIClient.h"
#include "AnimationEditorController.h"
#include "AnimationEditorPanel.h"
#include "AnimationPreviewWidget.h"
#include "AnimationImport.h"
#include "../shared/ResourceManager.h"
#include <QtCore/QEvent>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QSaveFile>
#include <QtCore/QSettings>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtCore/QJsonArray>
#include <QtCore/QUuid>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QProgressDialog>
#include <limits>
#include <future>
#include <atomic>
#include <chrono>
#include <cmath>
#include <stdexcept>

struct AnimationEditorController::Impl
{
	Impl(AnimationEditorController* owner_, AnimationEditorPanel* panel_, const std::string& base,
		QSettings* settings_, GUIClient* client_, std::function<void()> restore_)
	: owner(owner_), panel(panel_), settings(settings_), client(client_), restore(std::move(restore_))
	{
		driver_path = QString::fromStdString(base + "/data/resources/animations/Idle.subanim");
		for(const QString& file : QDir(QString::fromStdString(base + "/data/resources/animations")).entryList({"*.subanim"}, QDir::Files))
			reserved_names.push_back(QFileInfo(file).completeBaseName());
		preview = new AnimationPreviewWidget(base, settings, client, restore, panel);
		panel->setPreviewWidget(preview);
		QObject::connect(panel, &AnimationEditorPanel::animationSelected, owner, [this](const QString& id) { preview->setAnimation(toURLString(id.toStdString())); });
		QObject::connect(panel, &AnimationEditorPanel::previewPlaybackChanged, preview, &AnimationPreviewWidget::setPlaying);
		QObject::connect(panel, &AnimationEditorPanel::previewSeekRequested, preview, &AnimationPreviewWidget::seek);
		QObject::connect(panel, &AnimationEditorPanel::previewStepRequested, preview, &AnimationPreviewWidget::step);
		QObject::connect(panel, &AnimationEditorPanel::previewSpeedChanged, preview, &AnimationPreviewWidget::setSpeed);
		QObject::connect(panel, &AnimationEditorPanel::previewLoopChanged, preview, &AnimationPreviewWidget::setLoop);
		QObject::connect(preview, &AnimationPreviewWidget::positionChanged, panel, &AnimationEditorPanel::setPreviewPosition);
		QObject::connect(preview, &AnimationPreviewWidget::durationChanged, panel, &AnimationEditorPanel::setPreviewDuration);
		QObject::connect(preview, &AnimationPreviewWidget::playbackChanged, panel, &AnimationEditorPanel::setPreviewPlaying);
		QObject::connect(preview, &AnimationPreviewWidget::statusChanged, panel, &AnimationEditorPanel::setPreviewStatus);
		QObject::connect(panel, &AnimationEditorPanel::importRequested, owner, [this]() { chooseImport(); });
		QObject::connect(panel, &AnimationEditorPanel::saveCopyRequested, owner, [this](const QString& id, const QString& name, double speed) {
			if(!editable() || name.isEmpty()) { error(QObject::tr("Укажите название новой анимации и войдите в аккаунт.")); return; }
			if(nameExists(name)) { error(QObject::tr("Это имя уже занято. Укажите новое имя копии.")); return; }
			try { addFile(QString::fromStdString(client->resource_manager->pathForURL(toURLString(id.toStdString()))), name, speed, flagsFor(id)); }
			catch(const glare::Exception& e) { error(QString::fromStdString(e.what())); }
		});
		QObject::connect(panel, &AnimationEditorPanel::flagsEdited, owner, [this](const QString& id, bool loop, bool head) {
			if(!editable()) return;
			GestureSettings updated = gestures();
			for(auto& g : updated.gesture_settings)
				if(QString::fromStdString(toStdString(g.anim_URL)) == id)
					g.flags = (g.flags & ~(SingleGestureSettings::FLAG_LOOP | SingleGestureSettings::FLAG_ANIMATE_HEAD)) |
						(loop ? SingleGestureSettings::FLAG_LOOP : 0) | (head ? SingleGestureSettings::FLAG_ANIMATE_HEAD : 0);
			restore();
			client->gestureSettingsChanged(updated);
			revision = client->gesture_ui.getSettingsRevision(); // Don't reload/pause the preview for flag edits.
		});
		QObject::connect(panel, &AnimationEditorPanel::removeRequested, owner, [this](const QString& id) {
			if(!editable() || QMessageBox::question(panel, QObject::tr("Удалить жест"),
				QObject::tr("Убрать выбранный жест из списка? Исходный файл и ресурс останутся на диске.")) != QMessageBox::Yes) return;
			GestureSettings updated = gestures();
			for(auto it = updated.gesture_settings.begin(); it != updated.gesture_settings.end(); ++it)
				if(QString::fromStdString(toStdString(it->anim_URL)) == id)
				{
					restore();
					client->gesture_ui.stopAnyGesturePlaying();
					if(client->world_state.nonNull() && client->client_thread.nonNull())
						client->stopGestureClicked(it->friendly_name);
					updated.gesture_settings.erase(it);
					client->gestureSettingsChanged(updated);
					refresh(true);
					break;
				}
		});
		QObject::connect(panel, &AnimationEditorPanel::performRequested, owner, [this](const QString& id) {
			if(!editable() || client->world_state.isNull() || client->client_thread.isNull()) return;
			for(const auto& g : gestures().gesture_settings)
				if(QString::fromStdString(toStdString(g.anim_URL)) == id)
				{
					restore();
					client->gesture_ui.stopAnyGesturePlaying();
					client->performGestureClicked(g.friendly_name, g.anim_URL, (g.flags & SingleGestureSettings::FLAG_ANIMATE_HEAD) != 0, (g.flags & SingleGestureSettings::FLAG_LOOP) != 0);
					break;
				}
		});
		QObject::connect(panel, &AnimationEditorPanel::stopRequested, owner, [this]() {
			if(!editable() || client->world_state.isNull() || client->client_thread.isNull()) return;
			restore(); client->gesture_ui.stopAnyGesturePlaying(); client->stopGestureClicked("");
		});
		QObject::connect(panel, &AnimationEditorPanel::exportRequested, owner, [this](const QString& id) { exportFile(id); });
		poll.setInterval(1000);
		QObject::connect(&poll, &QTimer::timeout, owner, [this]() { refresh(false); });
		completion.setInterval(50);
		QObject::connect(&completion, &QTimer::timeout, owner, [this]() { completeImport(); });
	}
	bool editable() const { return !stopped && client->isLoggedIn(); }
	bool nameExists(const QString& name) const
	{
		if(reserved_names.contains(name)) return true;
		for(const auto& g : gestures().gesture_settings) if(QString::fromStdString(g.friendly_name) == name) return true;
		return false;
	}
	const GestureSettings& gestures() const { return client->gesture_ui.getCurrentGestureSettings(); }
	uint32 flagsFor(const QString& id) const
	{
		for(const auto& g : gestures().gesture_settings) if(QString::fromStdString(toStdString(g.anim_URL)) == id) return g.flags;
		return 0;
	}
	void error(const QString& text)
	{
		panel->setPreviewStatus(text);
		if(mcp_conversion) { last_save.insert("status", "failed"); last_save.insert("error", text); }
		else QMessageBox::warning(panel, QObject::tr("Анимации"), text);
	}
	void refresh(bool force, const QString& selected = QString())
	{
		if(stopped) return;
		panel->setEditingEnabled(editable());
		const uint64 current = client->gesture_ui.getSettingsRevision();
		if(!force && revision == current && user == client->logged_in_user_id && server == client->server_hostname) return;
		revision = current;
		QVector<AnimationEditorItem> items;
		for(const auto& g : gestures().gesture_settings)
			items.push_back({QString::fromStdString(toStdString(g.anim_URL)), QString::fromStdString(g.friendly_name), g.anim_duration,
				(g.flags & SingleGestureSettings::FLAG_LOOP) != 0, (g.flags & SingleGestureSettings::FLAG_ANIMATE_HEAD) != 0});
		panel->setAnimations(items, selected);
		// Conversion captures its own session; don't silently rebind the transaction on login changes.
		if(!temporary) { user = client->logged_in_user_id; server = client->server_hostname; }
	}
	void addFile(const QString& path, const QString& name, double speed, uint32 flags, bool from_mcp = false)
	{
		if(!editable() || temporary) return;
		mcp_conversion = from_mcp;
		if(from_mcp) {
			last_save = QJsonObject{{"job_id", QUuid::createUuid().toString(QUuid::WithoutBraces)}, {"status", "pending"}, {"name", name}};
			save_user = client->logged_in_user_id; save_server = client->server_hostname; save_world = client->server_worldname;
		}
		try
		{
			if(gestures().gesture_settings.size() + 1 >= GestureSettings::MAX_GESTURE_SETTINGS_SIZE) throw glare::Exception("Gesture list is full.");
			if(nameExists(name)) throw glare::Exception("An animation with this name already exists. Choose a different name.");
			temporary.reset(new QTemporaryDir());
			if(!temporary->isValid()) throw glare::Exception("Cannot create temporary animation directory.");
			const QString output = temporary->filePath("animation.subanim");
			const QString driver = driver_path;
			user = client->logged_in_user_id; server = client->server_hostname;
			import_world = client->server_worldname;
			import_flags = flags;
			cancelled = std::make_shared<std::atomic_bool>(false);
			const auto stop = cancelled;
			preview->setPlaying(false);
			panel->setBusy(true);
			panel->setPreviewStatus(QObject::tr("Чтение и преобразование анимации внутри клиента…"));
			progress = new QProgressDialog(QObject::tr("Импорт анимации. Blender не требуется."), QObject::tr("Отмена"), 0, 0, panel);
			progress->setWindowModality(Qt::NonModal);
			progress->setMinimumDuration(0);
			QObject::connect(progress, &QProgressDialog::canceled, owner, [stop]() { *stop = true; });
			job = std::async(std::launch::async, [path, output, name, speed, stop, driver]() {
				const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
				return AnimationImport::importFile(path, output, name, speed, [stop, deadline]() {
					return stop->load() || std::chrono::steady_clock::now() >= deadline;
				}, driver);
			});
			completion.start();
		}
		catch(const glare::Exception& e) { error(QString::fromStdString(e.what())); finishConversion(); }
		catch(const std::exception& e) { error(QString::fromUtf8(e.what())); finishConversion(); }
	}
	void completeImport()
	{
		if(!job.valid() || job.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
		completion.stop();
		try {
			const auto result = job.get();
			if(!cancelled->load() && editable() && user == client->logged_in_user_id && server == client->server_hostname && import_world == client->server_worldname) {
				if(nameExists(result.name)) throw glare::Exception("Имя анимации уже занято.");
				if(gestures().gesture_settings.size() + 1 >= GestureSettings::MAX_GESTURE_SETTINGS_SIZE) throw glare::Exception("Gesture list is full.");
				SingleGestureSettings item;
				item.friendly_name = result.name.toStdString();
				item.anim_duration = result.duration_seconds;
				item.flags = import_flags;
				item.anim_URL = client->resource_manager->copyLocalFileToResourceDirAndReturnURL(result.subanim_path.toStdString());
				GestureSettings updated = gestures();
				updated.gesture_settings.push_back(item);
				restore(); client->gestureSettingsChanged(updated);
				refresh(true, QString::fromStdString(toStdString(item.anim_URL)));
				if(mcp_conversion) { last_save.insert("status", "completed"); last_save.insert("animation_url", QString::fromStdString(toStdString(item.anim_URL))); }
			} else {
				panel->setPreviewStatus(QObject::tr("Импорт отменён или изменился аккаунт/сервер. Жест не добавлен."));
				if(mcp_conversion) last_save.insert("status", "cancelled");
			}
		}
		catch(const glare::Exception& e) { if(!cancelled->load()) error(QString::fromStdString(e.what())); }
		catch(const std::exception& e) { if(!cancelled->load()) error(QString::fromUtf8(e.what())); }
		if(cancelled->load()) {
			panel->setPreviewStatus(QObject::tr("Импорт отменён. Исходный файл не изменён."));
			if(mcp_conversion) last_save.insert("status", "cancelled");
		}
		finishConversion();
	}
	void chooseImport()
	{
		if(!editable() || temporary) return;
		const QString path = QFileDialog::getOpenFileName(panel, QObject::tr("Импорт анимации в жесты"), QString(), QObject::tr("Анимации (*.glb *.subanim *.fbx)"));
		if(path.isEmpty() || !editable()) return;
		QString name = QFileInfo(path).completeBaseName();
		const QString stem = name;
		for(int n = 2;; ++n)
		{
			if(!nameExists(name)) break;
			name = stem + QStringLiteral(" (%1)").arg(n);
		}
		addFile(path, name, 1, 0);
	}
	void finishConversion()
	{
		completion.stop();
		if(progress) { progress->close(); progress->deleteLater(); progress = nullptr; }
		temporary.reset();
		if(!stopped) panel->setBusy(false);
		mcp_conversion = false;
	}
	void exportFile(const QString& id)
	{
		try
		{
			const QString source = QString::fromStdString(client->resource_manager->pathForURL(toURLString(id.toStdString())));
			const QString destination = QFileDialog::getSaveFileName(panel, QObject::tr("Экспорт жеста"), QStringLiteral("gesture.subanim"), QObject::tr("Анимация (*.subanim)"));
			if(destination.isEmpty()) return;
			QFile input(source);
			if(!input.open(QIODevice::ReadOnly) || input.size() > 64 * 1024 * 1024) throw glare::Exception("Animation resource is unavailable or too large.");
			QSaveFile output(destination);
			const QByteArray data = input.readAll();
			if(input.error() != QFileDevice::NoError || !output.open(QIODevice::WriteOnly) || output.write(data) != data.size() || !output.commit()) throw glare::Exception("Could not export animation.");
		}
		catch(const glare::Exception& e) { error(QString::fromStdString(e.what())); }
	}
	AnimationEditorController* owner;
	AnimationEditorPanel* panel;
	AnimationPreviewWidget* preview;
	QSettings* settings;
	GUIClient* client;
	std::function<void()> restore;
	QTimer poll, completion;
	std::future<AnimationImport::Result> job;
	std::shared_ptr<std::atomic_bool> cancelled;
	QProgressDialog* progress = nullptr;
	std::unique_ptr<QTemporaryDir> temporary;
	uint32 import_flags = 0;
	QStringList reserved_names;
	QString driver_path;
	UserID user;
	std::string server;
	std::string import_world;
	QJsonObject last_save;
	UserID save_user;
	std::string save_server, save_world;
	bool mcp_conversion = false;
	uint64 revision = std::numeric_limits<uint64>::max();
	bool stopped = false;
};

AnimationEditorController::AnimationEditorController(AnimationEditorPanel* panel, const std::string& base,
	QSettings* settings, GUIClient* client, std::function<void()> restore)
: QObject(panel), impl(new Impl(this, panel, base, settings, client, std::move(restore)))
{
	panel->installEventFilter(this);
	if(panel->isVisible()) { impl->refresh(true); impl->poll.start(); }
}

AnimationEditorController::~AnimationEditorController() { shutdown(); }

QJsonObject AnimationEditorController::handleMCPRequest(const QString& tool, const QJsonObject& args)
{
	if(impl->stopped) throw std::runtime_error("Animation editor has shut down.");
	impl->refresh(false);
	const auto snapshot = [this]() {
		QJsonArray animations;
		for(const auto& g : impl->gestures().gesture_settings)
		{
			const QString url = QString::fromStdString(toStdString(g.anim_URL));
			const bool available = impl->client->resource_manager.nonNull() &&
				QFileInfo::exists(QString::fromStdString(impl->client->resource_manager->pathForURL(g.anim_URL)));
			animations.append(QJsonObject{{"animation_url", url}, {"name", QString::fromStdString(g.friendly_name)},
				{"duration_seconds", g.anim_duration}, {"loop", (g.flags & SingleGestureSettings::FLAG_LOOP) != 0},
				{"animate_head", (g.flags & SingleGestureSettings::FLAG_ANIMATE_HEAD) != 0}, {"locally_available", available}});
		}
		const bool same_session = impl->save_user == impl->client->logged_in_user_id && impl->save_server == impl->client->server_hostname && impl->save_world == impl->client->server_worldname;
		return QJsonObject{{"animations", animations}, {"preview", impl->panel->previewState()},
			{"can_edit", impl->editable()}, {"last_save", same_session ? impl->last_save : QJsonObject()}, {"scope", "Current user's gesture library and native Qt animation editor"}};
	};
	if(tool == "animation_editor_list")
	{
		if(!args.isEmpty()) throw std::runtime_error("animation_editor_list takes no arguments.");
		return snapshot();
	}
	if(tool != "animation_editor_control") throw std::runtime_error("Unknown animation editor tool.");
	const QString action = args.value("action").toString();
	const QStringList actions{"select", "play", "pause", "seek", "speed", "flags", "save_copy", "perform", "stop"};
	if(!actions.contains(action)) throw std::runtime_error("Unknown animation editor action.");
	QStringList fields{"action", "animation_url"};
	if(action == "seek") fields.append("position");
	if(action == "speed" || action == "save_copy") fields.append("speed");
	if(action == "save_copy") fields.append("name");
	if(action == "flags") fields.append({"loop", "animate_head"});
	for(auto it = args.begin(); it != args.end(); ++it)
		if(!fields.contains(it.key())) throw std::runtime_error("Unexpected argument for this animation editor action.");
	if(args.contains("animation_url") && !args.value("animation_url").isString()) throw std::runtime_error("animation_url must be a resource URL string.");
	if(impl->temporary) throw std::runtime_error("Animation conversion is in progress. Poll animation_editor_list for last_save status.");
	if(action == "flags" || action == "save_copy" || action == "perform" || action == "stop")
		if(!impl->editable()) throw std::runtime_error("Log in before changing or performing gestures.");
	if(action == "stop")
	{
		if(impl->client->world_state.isNull() || impl->client->client_thread.isNull()) throw std::runtime_error("No connected world.");
		impl->panel->stopRequested();
		return snapshot();
	}
	const QString id = args.value("animation_url").toString(impl->panel->selectedAnimationId());
	if(id.isEmpty()) throw std::runtime_error("Select an animation_url returned by animation_editor_list.");
	bool found = false;
	for(const auto& g : impl->gestures().gesture_settings)
		if(QString::fromStdString(toStdString(g.anim_URL)) == id) { found = true; break; }
	if(!found) throw std::runtime_error("Animation URL is not in the current user's gesture library.");
	if(!impl->panel->selectAnimation(id)) throw std::runtime_error("Animation selection is unavailable.");
	const double speed = args.value("speed").toDouble(impl->panel->previewState().value("speed").toDouble(1));
	if(action == "speed" || action == "save_copy")
		if((args.contains("speed") && !args.value("speed").isDouble()) || !std::isfinite(speed) || speed < 0.05 || speed > 4)
			throw std::runtime_error("speed must be a number from 0.05 to 4.");
	if(action == "play" || action == "seek")
		if(!impl->panel->isVisible() || impl->panel->previewState().value("duration_seconds").toDouble() <= 0)
			throw std::runtime_error("Preview is not ready. Select the animation, then poll animation_editor_list until preview.duration_seconds is positive; inspect preview.status on error.");
	if(action == "play") impl->preview->setPlaying(true);
	else if(action == "pause") impl->preview->setPlaying(false);
	else if(action == "seek")
	{
		const double position = args.value("position").toDouble(-1);
		if(!std::isfinite(position) || position < 0 || position > 1) throw std::runtime_error("position is required and must be a number from 0 to 1.");
		impl->preview->seek(position);
	}
	else if(action == "speed")
	{
		if(!args.contains("speed")) throw std::runtime_error("speed is required.");
		impl->panel->setPreviewSpeed(speed);
	}
	else if(action == "flags")
	{
		if(!args.contains("loop") && !args.contains("animate_head")) throw std::runtime_error("Specify loop or animate_head.");
		for(const QString& key : {QString("loop"), QString("animate_head")})
			if(args.contains(key) && !args.value(key).isBool()) throw std::runtime_error("Animation flags must be boolean.");
		const uint32 flags = impl->flagsFor(id);
		impl->panel->setAnimationFlags(args.value("loop").toBool((flags & SingleGestureSettings::FLAG_LOOP) != 0),
			args.value("animate_head").toBool((flags & SingleGestureSettings::FLAG_ANIMATE_HEAD) != 0));
	}
	else if(action == "perform")
	{
		if(impl->client->world_state.isNull() || impl->client->client_thread.isNull()) throw std::runtime_error("No connected world.");
		impl->panel->performRequested(id);
	}
	else if(action == "save_copy")
	{
		const QString name = args.value("name").toString().trimmed();
		if(name.isEmpty() || name.size() > 160 || name.toUtf8().size() > 256 || name.contains(QChar::Null)) throw std::runtime_error("name must contain 1..160 characters, at most 256 UTF-8 bytes, and no NUL.");
		if(impl->nameExists(name)) throw std::runtime_error("Animation name is already used. Choose a new name.");
		if(impl->client->resource_manager.isNull()) throw std::runtime_error("Resource manager is not available.");
		const QString path = QString::fromStdString(impl->client->resource_manager->pathForURL(toURLString(id.toStdString())));
		if(!QFileInfo::exists(path)) throw std::runtime_error("Animation resource is not downloaded locally yet.");
		impl->addFile(path, name, speed, impl->flagsFor(id), true);
		if(impl->last_save.value("status").toString() == "failed") throw std::runtime_error(impl->last_save.value("error").toString().toStdString());
	}
	return snapshot();
}

void AnimationEditorController::shutdown()
{
	if(impl->stopped) return;
	impl->stopped = true;
	impl->poll.stop(); impl->completion.stop();
	if(impl->cancelled) *impl->cancelled = true;
	if(impl->job.valid()) impl->job.wait(); // Cooperative cancellation; temp files outlive the worker.
	impl->finishConversion();
	impl->preview->shutdownGL();
}

bool AnimationEditorController::eventFilter(QObject* watched, QEvent* event)
{
	if(!impl->stopped && watched == impl->panel)
	{
		if(event->type() == QEvent::Show) { impl->refresh(true); impl->poll.start(); }
		else if(event->type() == QEvent::Hide) impl->poll.stop();
	}
	return QObject::eventFilter(watched, event);
}
