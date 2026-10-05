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
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QProgressDialog>
#include <limits>
#include <future>
#include <atomic>
#include <chrono>

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
	void error(const QString& text) { panel->setPreviewStatus(text); QMessageBox::warning(panel, QObject::tr("Анимации"), text); }
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
	void addFile(const QString& path, const QString& name, double speed, uint32 flags)
	{
		if(!editable() || temporary) return;
		try
		{
			if(gestures().gesture_settings.size() + 1 >= GestureSettings::MAX_GESTURE_SETTINGS_SIZE) throw glare::Exception("Gesture list is full.");
			if(nameExists(name)) throw glare::Exception("An animation with this name already exists. Choose a different name.");
			temporary.reset(new QTemporaryDir());
			if(!temporary->isValid()) throw glare::Exception("Cannot create temporary animation directory.");
			const QString output = temporary->filePath("animation.subanim");
			const QString driver = driver_path;
			user = client->logged_in_user_id; server = client->server_hostname;
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
		catch(const glare::Exception& e) { finishConversion(); error(QString::fromStdString(e.what())); }
		catch(const std::exception& e) { finishConversion(); error(QString::fromUtf8(e.what())); }
	}
	void completeImport()
	{
		if(!job.valid() || job.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
		completion.stop();
		try {
			const auto result = job.get();
			if(!cancelled->load() && editable() && user == client->logged_in_user_id && server == client->server_hostname) {
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
			} else panel->setPreviewStatus(QObject::tr("Импорт отменён или изменился аккаунт/сервер. Жест не добавлен."));
		}
		catch(const glare::Exception& e) { if(!cancelled->load()) error(QString::fromStdString(e.what())); }
		catch(const std::exception& e) { if(!cancelled->load()) error(QString::fromUtf8(e.what())); }
		if(cancelled->load()) panel->setPreviewStatus(QObject::tr("Импорт отменён. Исходный файл не изменён."));
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
