#include "../gui_client/EditorTextSync.h"
#include <QtWidgets/QApplication>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QPlainTextEdit>
#include <stdexcept>
#include <iostream>

#define require(value) do { if(!(value)) throw std::runtime_error("Editor text sync regression: " #value); } while(false)

template<class TextEdit> void checkEditor()
{
	TextEdit editor;
	syncEditorPlainText(&editor, QString::fromUtf8("Привет"), false);
	QTextCursor cursor = editor.textCursor();
	cursor.setPosition(3);
	editor.setTextCursor(cursor);
	editor.insertPlainText("!");
	const QString edited = editor.toPlainText();
	syncEditorPlainText(&editor, edited, true);
	require(editor.textCursor().position() == 4);
	require(editor.document()->isUndoAvailable());
	editor.undo();
	require(editor.toPlainText() == QString::fromUtf8("Привет"));
	cursor = editor.textCursor(); cursor.setPosition(5); cursor.setPosition(2, QTextCursor::KeepAnchor); editor.setTextCursor(cursor);
	syncEditorPlainText(&editor, QString::fromUtf8("Привет"), true);
	require(editor.textCursor().anchor() == 5 && editor.textCursor().position() == 2);
	syncEditorPlainText(&editor, QString::fromUtf8("Привет мир"), true);
	require(editor.textCursor().anchor() == 5 && editor.textCursor().position() == 2);
	syncEditorPlainText(&editor, "a", true);
	require(editor.textCursor().position() == 1 && editor.textCursor().anchor() == 1);
	syncEditorPlainText(&editor, "a", false);
	require(editor.textCursor().position() == 0 && !editor.document()->isUndoAvailable());
}

int main(int argc, char** argv)
{
	try
	{
		QApplication application(argc, argv);
		checkEditor<QTextEdit>();
		checkEditor<QPlainTextEdit>();
		std::cout << "Content and script editor cursor/selection/undo/remote-update checks passed\n";
	}
	catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
