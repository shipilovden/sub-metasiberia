#include "../AIMessageInput.h"

#include <QtGui/QClipboard>
#include <QtWidgets/QApplication>

#include <iostream>

namespace
{
void require(bool condition, const char* message)
{
	if(!condition)
	{
		std::cerr << "FAIL: " << message << "\n";
		std::exit(1);
	}
}

void sendKey(AIMessageInput& input, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
	// Native Qt keyboard events carry text along with the key code. In particular,
	// QPlainTextEdit uses the Return event text when inserting a Shift+Enter newline.
	const QString text = (key == Qt::Key_Return || key == Qt::Key_Enter) ? QStringLiteral("\n") : QString();
	QKeyEvent event(QEvent::KeyPress, key, modifiers, text);
	QApplication::sendEvent(&input, &event);
}
}

int main(int argc, char** argv)
{
	QApplication app(argc, argv);
	AIMessageInput input;
	int submit_count = 0;
	input.submit = [&submit_count]() { ++submit_count; };

	input.setPlainText(QStringLiteral("first line"));
	input.moveCursor(QTextCursor::End);
	sendKey(input, Qt::Key_Return, Qt::ShiftModifier);
	require(input.toPlainText() == QStringLiteral("first line\n"), "Shift+Enter must insert a newline without submitting.");
	require(submit_count == 0, "Shift+Enter must not submit the message.");

	const QString clipboard_text = QStringLiteral("pasted first line\npasted second line\npasted third line");
	QApplication::clipboard()->setText(clipboard_text);
	input.clear();
	sendKey(input, Qt::Key_V, Qt::ControlModifier);
	require(input.toPlainText() == clipboard_text, "Pasting multiline text must preserve all line breaks.");

	const QString before_submit = input.toPlainText();
	sendKey(input, Qt::Key_Return);
	require(submit_count == 1, "Enter must invoke the submit handler exactly once.");
	require(input.toPlainText() == before_submit, "Enter must not insert an unwanted newline.");

	std::cout << "PASS: Shift+Enter newline, multiline clipboard paste, and Enter submission semantics.\n";
	return 0;
}
