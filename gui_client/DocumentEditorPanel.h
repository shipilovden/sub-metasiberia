/*=====================================================================
DocumentEditorPanel.h
---------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "DocumentObjectSettings.h"
#include <QtCore/QString>
#include <QtWidgets/QWidget>


class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTabWidget;
class QTextEdit;
class QToolButton;

#if defined(METASIBERIA_QT_PDF_AVAILABLE)
class QPdfDocument;
class QPdfView;
#endif


// Native Qt editor for Metasiberia text documents.  Qt PDF support is an
// optional compile-time capability because the canonical Qt 5.15 installation
// does not currently contain QtPdf/QtPdfWidgets.
class DocumentEditorPanel final : public QWidget
{
	Q_OBJECT

public:
	enum DocumentFormat
	{
		FormatPlainText,
		FormatHtml,
		FormatMarkdown,
		FormatPdf,
		FormatUnknown
	};
	Q_ENUM(DocumentFormat)

	explicit DocumentEditorPanel(QWidget* parent = 0);
	~DocumentEditorPanel();

	void setIconDirectory(const QString& directory);
	void setReadOnly(bool read_only);
	void clearDocument();

	bool loadFile(const QString& path);
	bool saveDocument();
	bool exportDocument(DocumentFormat format, const QString& path = QString());

	DocumentObjectSettings documentSettings() const;
	void setDocumentSettings(const DocumentObjectSettings& settings);

	QString currentFilePath() const { return current_file_path; }
	DocumentFormat currentFormat() const { return current_format; }
	bool hasUnsavedChanges() const { return document_modified; }
	static bool hasQtPdfSupport();

signals:
	void documentOpened(const QString& path, const QString& format_id);
	void documentSaved(const QString& path, const QString& format_id);
	void documentModifiedChanged(bool modified);
	void descriptorChanged();
	void statusMessageChanged(const QString& message);
	void errorOccurred(const QString& message);
	void linkedObjectSelectionRequested(const QString& current_object_id);
	void pdfPageTextureRequested(const QString& pdf_path, int zero_based_page);

private:
	void buildUi();
	QWidget* buildMetadataPage();
	QWidget* buildPdfPage();
	void applyIcons();
	void updateUiState();
	void setModified(bool modified);
	void showStatus(const QString& message, bool error = false);
	void openDocumentDialog();
	void showSourceMode(bool source_mode);
	void renderPreviewFromSource();
	void syncSourceFromPreview();
	void findText(bool backwards);
	void requestLinkedObjectSelection();
	bool maybeDiscardChanges();
	bool saveTextToPath(const QString& path, DocumentFormat format, bool adopt_as_current_document);
	QString textForFormat(DocumentFormat format);
	void setCurrentDocument(const QString& path, DocumentFormat format);
	void setPdfPath(const QString& path);

	static DocumentFormat formatFromPath(const QString& path);
	static QString formatId(DocumentFormat format);
	static QString formatName(DocumentFormat format);
	static QString fileDialogFilter(DocumentFormat format);

	QString icon_directory;
	QString current_file_path;
	DocumentObjectSettings current_settings;
	DocumentFormat current_format;
	bool document_modified;
	bool updating_editors;
	bool read_only;

	QToolButton* open_button;
	QToolButton* save_button;
	QToolButton* export_button;
	QToolButton* source_mode_button;
	QToolButton* previous_match_button;
	QToolButton* next_match_button;
	QLineEdit* search_edit;
	QLabel* file_label;
	QLabel* status_label;
	QTabWidget* tabs;
	QStackedWidget* editor_stack;
	QTextEdit* source_editor;
	QTextEdit* preview_editor;

	QLineEdit* title_edit;
	QLineEdit* author_edit;
	QTextEdit* description_edit;
	QLineEdit* language_edit;
	QLineEdit* tags_edit;
	QLineEdit* source_url_edit;
	QLineEdit* resource_url_edit;
	QLineEdit* linked_object_edit;
	QToolButton* select_linked_object_button;
	QTextEdit* sources_edit;
	QTextEdit* comments_edit;
	QComboBox* display_mode_combo;
	QCheckBox* open_on_click_check;
	QCheckBox* pinned_to_object_check;
	QCheckBox* allow_selection_check;
	QCheckBox* show_toolbar_check;
	QSpinBox* initial_page_spin;
	QDoubleSpinBox* zoom_spin;
	QDoubleSpinBox* screen_width_spin;
	QDoubleSpinBox* screen_height_spin;
	QDoubleSpinBox* rotation_spin;

	QLineEdit* pdf_path_edit;
	QLabel* pdf_status_label;
	QPushButton* open_pdf_button;
	QPushButton* page_texture_button;

#if defined(METASIBERIA_QT_PDF_AVAILABLE)
	QPdfDocument* pdf_document;
	QPdfView* pdf_view;
#endif
};
