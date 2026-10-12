#pragma once

#include <QtCore/QSignalBlocker>
#include <QtGui/QTextCursor>
#include <QtGui/QTextDocument>
#include <QtWidgets/QScrollBar>

// Server echoes must not recreate the document after each keystroke: doing so
// resets the cursor, selection, undo history and an active input-method edit.
// A different object starts a fresh document; an actual update to the same
// object preserves the current selection and viewport as far as possible.
template<class TextEdit>
void syncEditorPlainText(TextEdit* editor, const QString& text, bool same_object)
{
	if(same_object && editor->toPlainText() == text)
		return;

	const QSignalBlocker blocker(editor);
	// QTextCursor copies track document edits too, so capture numeric positions
	// before setPlainText clears the document.
	const int old_position = editor->textCursor().position();
	const int old_anchor = editor->textCursor().anchor();
	const int vertical_scroll = editor->verticalScrollBar()->value();
	const int horizontal_scroll = editor->horizontalScrollBar()->value();
	editor->setPlainText(text);
	if(same_object)
	{
		QTextCursor cursor = editor->textCursor();
		const int last_position = editor->document()->characterCount() - 1;
		cursor.setPosition(qMin(old_anchor, last_position));
		cursor.setPosition(qMin(old_position, last_position), QTextCursor::KeepAnchor);
		editor->setTextCursor(cursor);
		editor->verticalScrollBar()->setValue(vertical_scroll);
		editor->horizontalScrollBar()->setValue(horizontal_scroll);
	}
}
