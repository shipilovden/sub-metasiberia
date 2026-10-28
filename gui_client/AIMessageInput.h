/*=====================================================================
AIMessageInput.h
----------------
Multiline input widget with Enter-to-submit behaviour for the AI dock.
=====================================================================*/
#pragma once

#include <functional>

#include <QtGui/QKeyEvent>
#include <QtWidgets/QPlainTextEdit>

class AIMessageInput final : public QPlainTextEdit
{
public:
	explicit AIMessageInput(QWidget* parent = nullptr) : QPlainTextEdit(parent) {}

	std::function<void()> submit;

protected:
	void keyPressEvent(QKeyEvent* event) override
	{
		if(event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
		{
			if(event->modifiers() & Qt::ShiftModifier)
			{
				QTextCursor cursor = textCursor();
				cursor.insertText(QStringLiteral("\n"));
				setTextCursor(cursor);
			}
			else if(submit)
			{
				submit();
			}
			event->accept();
			return;
		}
		QPlainTextEdit::keyPressEvent(event);
	}
};
