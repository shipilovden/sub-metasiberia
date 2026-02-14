#pragma once

#include <QtWidgets/QDialog>
#include <atomic>

#include "HTTPClient.h"

class QLabel;
class QPushButton;
class QProgressBar;
class QTextEdit;
class QCloseEvent;

class UpdateManager;


class UpdateDialog final : public QDialog
{
	Q_OBJECT
public:
	UpdateDialog(UpdateManager* update_manager_, QWidget* parent = nullptr);

protected:
	void closeEvent(QCloseEvent* event) override;

private slots:
	void refreshUI();
	void onCheckClicked();
	void onDownloadClicked();
	void onCancelDownloadClicked();

	void onDownloadProgress(qint64 received, qint64 total);

private:
	void setStatusText(const QString& s);
	void setControlsEnabled(bool enabled);
	QString defaultDownloadPath() const;
	void finishDownload(bool ok, const QString& error);

	UpdateManager* update_manager = nullptr;

	std::atomic<bool> download_in_progress { false };
	std::atomic<bool> cancel_requested { false };
	HTTPClientRef dl_client;

	QLabel* current_version_label = nullptr;
	QLabel* latest_version_label = nullptr;
	QLabel* status_label = nullptr;
	QTextEdit* notes_text = nullptr;
	QProgressBar* progress = nullptr;

	QPushButton* check_button = nullptr;
	QPushButton* download_button = nullptr;
	QPushButton* cancel_button = nullptr;
	QPushButton* close_button = nullptr;

	QString downloading_path_part;
	QString downloading_path_final;
};
