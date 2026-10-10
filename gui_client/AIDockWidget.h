/*=====================================================================
AIDockWidget.h
--------------
Right-side Codex chat panel for the Metasiberia client.
=====================================================================*/
#pragma once


#include "CodexAppServerClient.h"
#include <QtWidgets/QDockWidget>
#include <QtCore/QTemporaryDir>
#include <QtCore/QHash>
#include <QtCore/QElapsedTimer>


class QLabel;
class QGroupBox;
class QLineEdit;
class QPushButton;
class QTextBrowser;
class QTimer;
class QComboBox;
class QListWidget;
class QMimeData;
class QImage;
class QTabWidget;
class QCheckBox;
class QSpinBox;


class AIDockWidget final : public QDockWidget
{
	Q_OBJECT
public:
	explicit AIDockWidget(QWidget* parent = nullptr);

	void openForEndpoint(const QString& endpoint, bool mcp_enabled, bool mcp_running, bool world_connected, const QString& working_directory);
	void retranslateUi();
	void configureMCPSettings(bool enabled, int port, const QString& icon_directory);
	void stopCodex();
	void setPolyHavenEnabled(bool enabled);

signals:
	void mcpSettingsChanged(bool enabled, int port);
	void polyHavenChanged(bool enabled);

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
	void dragEnterEvent(QDragEnterEvent* event) override;
	void dropEvent(QDropEvent* event) override;
	void changeEvent(QEvent* event) override;

private slots:
	void sendCurrentMessage();
	void appendAssistantDelta(const QString& delta);
	void finishAssistantMessage();
	void updateStatus(const QString& status);
	void showError(const QString& message);

private:
	void appendLine(const QString& speaker, const QString& text);
	void addImage(const QImage& image, const QString& name);
	void addImageFile(const QString& path);
	void addMimeImages(const QMimeData* data);
	void updateModels(const QJsonArray& models);
	void updateEfforts();
	void updateIcons();
	void openReference(const QString& path);
	void appendReferences(const QStringList& paths);
	void setTurnActive(bool active);
	void updateThinkingIndicator();
	QWidget* thinking_row;
	QLabel* thinking_icon;
	QLabel* thinking_label;
	QTimer* thinking_timer = nullptr;
	QElapsedTimer thinking_elapsed;
	QString activity_phase;
	QHash<QString, QString> sent_references;
	bool turn_active = false;
	QTabWidget* tabs;
	QCheckBox* enable_mcp;
	QCheckBox* enable_poly_haven;
	QLabel* texture_description;
	QLabel* image_generation_description;
	QSpinBox* mcp_port;
	QLabel* port_label;
	QLabel* mcp_description;
	QString icon_directory;
	bool initial_layout_done = false;

	QLabel* connection_label;
	QGroupBox* help_box;
	QLabel* help_label;
	QTextBrowser* conversation_view;
	QLineEdit* input_edit;
	QPushButton* send_button;
	QPushButton* attach_button;
	QPushButton* remove_reference_button;
	QLabel* model_label;
	QLabel* effort_label;
	QComboBox* model_combo;
	QComboBox* effort_combo;
	QListWidget* references;
	QTemporaryDir reference_directory;
	CodexAppServerClient codex;
	QString assistant_buffer;
};
