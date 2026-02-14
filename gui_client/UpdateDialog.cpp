/*=====================================================================
UpdateDialog.cpp
----------------
UI for checking and downloading an installer update.
=====================================================================*/

#include "UpdateDialog.h"

#include "UpdateManager.h"

#include <FileHandle.h>

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QMetaObject>
#include <QtCore/QProcess>
#include <QtCore/QStandardPaths>
#include <QtCore/QTimer>
#include <QtCore/QCoreApplication>
#include <QtCore/QPointer>
#include <QtGui/QCloseEvent>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QVBoxLayout>

#include <chrono>
#include <climits>
#include <string>
#include <thread>


UpdateDialog::UpdateDialog(UpdateManager* update_manager_, QWidget* parent)
:	QDialog(parent),
	update_manager(update_manager_)
{
	setWindowTitle("Update");
	setModal(true);
	setMinimumWidth(520);

	current_version_label = new QLabel(this);
	latest_version_label = new QLabel(this);
	status_label = new QLabel(this);
	status_label->setWordWrap(true);

	notes_text = new QTextEdit(this);
	notes_text->setReadOnly(true);
	notes_text->setMinimumHeight(160);

	progress = new QProgressBar(this);
	progress->setVisible(false);
	progress->setMinimum(0);
	progress->setMaximum(0);

	check_button = new QPushButton("Check", this);
	download_button = new QPushButton("Download and install", this);
	cancel_button = new QPushButton("Cancel", this);
	close_button = new QPushButton("Close", this);

	cancel_button->setVisible(false);

	connect(check_button, &QPushButton::clicked, this, &UpdateDialog::onCheckClicked);
	connect(download_button, &QPushButton::clicked, this, &UpdateDialog::onDownloadClicked);
	connect(cancel_button, &QPushButton::clicked, this, &UpdateDialog::onCancelDownloadClicked);
	connect(close_button, &QPushButton::clicked, this, &QDialog::accept);

	auto* root = new QVBoxLayout();
	root->addWidget(new QLabel("<b>Metasiberia Update</b>", this));
	root->addWidget(current_version_label);
	root->addWidget(latest_version_label);
	root->addSpacing(6);
	root->addWidget(status_label);
	root->addSpacing(6);
	root->addWidget(new QLabel("Release notes:", this));
	root->addWidget(notes_text);
	root->addWidget(progress);

	auto* buttons = new QHBoxLayout();
	buttons->addWidget(check_button);
	buttons->addWidget(download_button);
	buttons->addWidget(cancel_button);
	buttons->addStretch(1);
	buttons->addWidget(close_button);
	root->addLayout(buttons);
	setLayout(root);

	if(update_manager)
	{
		connect(update_manager, &UpdateManager::updateCheckFinished, this, &UpdateDialog::refreshUI);
		connect(update_manager, &UpdateManager::updateAvailabilityChanged, this, &UpdateDialog::refreshUI);
	}

	// Defer initial UI population until after show(), so layout is stable.
	QTimer::singleShot(0, this, &UpdateDialog::refreshUI);
}


void UpdateDialog::closeEvent(QCloseEvent* event)
{
	if(download_in_progress.load())
	{
		// Don't allow the dialog to close while we have a background download thread that may post back to this object.
		QMessageBox::information(this, "Update", "Download is in progress. Cancel the download before closing this window.");
		event->ignore();
		return;
	}

	QDialog::closeEvent(event);
}


void UpdateDialog::setStatusText(const QString& s)
{
	status_label->setText(s);
}


void UpdateDialog::setControlsEnabled(bool enabled)
{
	check_button->setEnabled(enabled);
	download_button->setEnabled(enabled);
	close_button->setEnabled(enabled);
}


void UpdateDialog::refreshUI()
{
	if(!update_manager)
	{
		current_version_label->setText("Current version: unknown");
		latest_version_label->setText("Latest version: unknown");
		setStatusText("Update manager is not available.");
		download_button->setEnabled(false);
		return;
	}

	current_version_label->setText("Current version: v" + update_manager->currentVersionString());

	if(update_manager->checkInProgress())
	{
		latest_version_label->setText("Latest version: checking...");
		setStatusText("Checking for updates...");
		download_button->setEnabled(false);
		return;
	}

	if(!update_manager->hasCheckResult())
	{
		latest_version_label->setText("Latest version: not checked");
		setStatusText("Press Check to look for updates.");
		download_button->setEnabled(false);
		return;
	}

	if(!update_manager->lastErrorString().isEmpty())
	{
		latest_version_label->setText("Latest version: unknown");
		setStatusText("Could not check updates: " + update_manager->lastErrorString());
		download_button->setEnabled(false);
		return;
	}

	const UpdateManager::UpdateInfo& info = update_manager->latest();
	latest_version_label->setText("Latest version: " + info.tag);
	notes_text->setPlainText(info.notes.trimmed());

	if(update_manager->updateAvailable())
	{
		setStatusText("Update available.");
		download_button->setEnabled(!download_in_progress.load());
	}
	else
	{
		setStatusText("You are up to date.");
		download_button->setEnabled(false);
	}
}


void UpdateDialog::onCheckClicked()
{
	if(update_manager)
		update_manager->checkForUpdatesAsync(/*force=*/true);
	refreshUI();
}


QString UpdateDialog::defaultDownloadPath() const
{
	const QString temp_dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
	if(temp_dir.isEmpty())
		return QString();

	const QString file_name = update_manager ? update_manager->latest().installer.name : QString("MetasiberiaUpdate.exe");
	return QDir(temp_dir).filePath(file_name);
}


void UpdateDialog::onDownloadClicked()
{
#if !defined(_WIN32)
	QMessageBox::information(this, "Update", "Automatic updates are only supported on Windows for now.");
	return;
#endif

	if(!update_manager || !update_manager->updateAvailable())
	{
		QMessageBox::information(this, "Update", "No update is available.");
		return;
	}

	const UpdateManager::InstallerAsset asset = update_manager->latest().installer;
	if(!asset.url.isValid())
	{
		QMessageBox::warning(this, "Update", "Installer URL is invalid.");
		return;
	}

	downloading_path_final = defaultDownloadPath();
	if(downloading_path_final.isEmpty())
	{
		QMessageBox::warning(this, "Update", "Could not determine a temporary download path.");
		return;
	}

	downloading_path_part = downloading_path_final + ".part";

	// If a previous partial download exists, remove it.
	QFile::remove(downloading_path_part);

	setControlsEnabled(false);
	progress->setVisible(true);
	progress->setMinimum(0);
	progress->setMaximum(0); // indeterminate until we get total
	cancel_button->setVisible(true);
	cancel_button->setEnabled(true);

	setStatusText("Downloading installer...");

	download_in_progress = true;
	cancel_requested = false;

	dl_client = new HTTPClient();
	dl_client->user_agent = "metasiberia-client";
	dl_client->additional_headers.push_back("Accept: */*");

	const std::string url = std::string(asset.url.toString().toUtf8().constData());
	const std::string part_path = std::string(downloading_path_part.toUtf8().constData());

	const HTTPClientRef client = dl_client;
	QPointer<UpdateDialog> self(this);

	std::thread([self, client, url, part_path]() {
		bool ok = false;
		QString err;

		try
		{
			FileHandle out(part_path, "wb");

			struct Handler final : public HTTPClient::StreamingDataHandler
			{
				Handler(FILE* f_, QPointer<UpdateDialog> dlg_) : f(f_), dlg(dlg_) {}

				void haveContentLength(uint64 content_length) override
				{
					total = content_length;
					emitProgress();
				}

				void handleData(ArrayRef<uint8> chunk, const HTTPClient::ResponseInfo& /*response_info*/) override
				{
					if(chunk.size() == 0)
						return;

					const size_t wrote = fwrite(chunk.data(), 1, chunk.size(), f);
					if(wrote != chunk.size())
						throw glare::Exception("Failed to write update installer to disk.");

					received += (uint64)wrote;
					emitProgressThrottled();
				}

				void emitProgress()
				{
					if(!dlg)
						return;

					const uint64 r = received;
					const uint64 t = total;
					QMetaObject::invokeMethod(dlg, [dlg = dlg, r, t]() {
						if(dlg)
							dlg->onDownloadProgress((qint64)r, (qint64)t);
					}, Qt::QueuedConnection);
				}

				void emitProgressThrottled()
				{
					const auto now = std::chrono::steady_clock::now();
					if(now - last_emit >= std::chrono::milliseconds(250))
					{
						last_emit = now;
						emitProgress();
					}
				}

				FILE* f = nullptr;
				QPointer<UpdateDialog> dlg;
				uint64 received = 0;
				uint64 total = 0;
				std::chrono::steady_clock::time_point last_emit = std::chrono::steady_clock::now();
			};

			Handler handler(out.getFile(), self);
			HTTPClient::ResponseInfo resp = client->downloadFile(url, handler);
			if(resp.response_code != 200)
				throw glare::Exception("HTTP download failed (code " + std::to_string(resp.response_code) + "): " + resp.response_message);

			// Make sure we flush any buffered data and show final progress.
			fflush(out.getFile());
			handler.emitProgress();
			ok = true;
		}
		catch(glare::Exception& e)
		{
			err = QString::fromStdString(e.what());
		}

		if(!self)
			return;

		QMetaObject::invokeMethod(self, [self, ok, err]() {
			if(self)
				self->finishDownload(ok, err);
		}, Qt::QueuedConnection);
	}).detach();
}


void UpdateDialog::onCancelDownloadClicked()
{
	cancel_requested = true;
	cancel_button->setEnabled(false);
	setStatusText("Cancelling download...");

	if(dl_client.nonNull())
		dl_client->kill();
}


void UpdateDialog::onDownloadProgress(qint64 received, qint64 total)
{
	if(total > 0)
	{
		if(total <= INT_MAX)
		{
			progress->setMaximum((int)total);
			const qint64 clamped = (received < 0) ? 0 : ((received > total) ? total : received);
			progress->setValue((int)clamped);
		}
		else
		{
			// Fallback: show percentage when size doesn't fit in int.
			progress->setMaximum(1000);
			const qint64 clamped = (received < 0) ? 0 : ((received > total) ? total : received);
			const int v = (int)((clamped * 1000) / total);
			progress->setValue(v);
		}
	}
}


void UpdateDialog::finishDownload(bool ok, const QString& error)
{
	dl_client = nullptr;
	download_in_progress = false;

	cancel_button->setVisible(false);
	progress->setVisible(false);
	setControlsEnabled(true);

	if(!ok)
	{
		QFile::remove(downloading_path_part);

		if(cancel_requested.load())
			setStatusText("Download cancelled.");
		else if(!error.isEmpty())
			setStatusText("Download failed: " + error);
		else
			setStatusText("Download failed.");

		cancel_requested = false;
		return;
	}

	// Finalize file.
	QFile::remove(downloading_path_final);
	if(!QFile::rename(downloading_path_part, downloading_path_final))
	{
		setStatusText("Download finished, but could not finalize installer file.");
		return;
	}

	// Basic validation: check size if we have it.
	if(update_manager)
	{
		const qint64 expected = update_manager->latest().installer.size_B;
		if(expected > 0)
		{
			const qint64 actual = QFileInfo(downloading_path_final).size();
			if(actual != expected)
			{
				setStatusText("Downloaded file size mismatch. Aborting update.");
				QFile::remove(downloading_path_final);
				return;
			}
		}
	}

	setStatusText("Launching installer...");

	// Start installer and quit the app.
	const bool started = QProcess::startDetached(downloading_path_final, QStringList());
	if(!started)
	{
		setStatusText("Could not start installer.");
		return;
	}

	QTimer::singleShot(250, []() {
		QCoreApplication::quit();
	});
}
