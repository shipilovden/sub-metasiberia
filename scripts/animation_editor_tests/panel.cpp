#include "../../gui_client/AnimationEditorPanel.h"
#include <QtWidgets/QApplication>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QToolButton>
#include <QtCore/QRegularExpression>
#include <QtGui/QMouseEvent>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <stdexcept>
#include <iostream>
void require(bool v, const char* message = "panel check failed") { if(!v) throw std::runtime_error(message); }
void requireRussianToolTip(QWidget* widget)
{
	require(widget != nullptr, "missing control");
	QString tip = widget->toolTip();
	// Qt's internal spin-box editor inherits its parent's tooltip.
	for(auto* parent = widget->parentWidget(); tip.isEmpty() && parent; parent = parent->parentWidget())
		tip = parent->toolTip();
	require(QRegularExpression(QStringLiteral("[А-Яа-яЁё]")).match(tip).hasMatch(), "missing Russian tooltip");
}
void dragSplitter(QSplitter* splitter, int distance)
{
	auto* handle = splitter->handle(1);
	const QPoint start = handle->rect().center();
	QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, start);
	const QPoint end = start + QPoint(0, distance);
	QMouseEvent move(QEvent::MouseMove, end, handle->mapToGlobal(end), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
	QApplication::sendEvent(handle, &move);
	QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, handle->rect().center());
	QApplication::processEvents();
}
int main(int argc, char** argv)
try
{
	QApplication app(argc, argv);
	AnimationEditorPanel panel(nullptr);
	auto* preview = new QLabel(QStringLiteral("Предпросмотр аватара"));
	preview->setAlignment(Qt::AlignCenter);
	panel.setPreviewWidget(preview);
	panel.resize(480, 900);
	panel.show();
	app.processEvents();
	auto* splitter = panel.findChild<QSplitter*>("animationEditorSplitter");
	require(splitter && splitter->orientation() == Qt::Vertical && splitter->count() == 2, "missing vertical preview/library splitter");
	require(splitter->handleWidth() == 7 && splitter->handle(1)->height() == 7, "splitter handle must be 7px");
	require(splitter->handle(1)->cursor().shape() == Qt::SplitVCursor, "splitter resize cursor");
	requireRussianToolTip(splitter->handle(1));
	requireRussianToolTip(preview);
	require(!splitter->childrenCollapsible(), "splitter panes must remain usable");
	auto* library = panel.findChild<QWidget*>("animationLibraryPane");
	auto* table = panel.findChild<QTableWidget*>("animationLibrary");
	auto* search = panel.findChild<QLineEdit*>("animationSearch");
	require(splitter->widget(0)->isAncestorOf(preview) && splitter->widget(1) == library, "splitter pane ownership");
	require(library->isAncestorOf(table) && library->isAncestorOf(search), "library controls must resize together");
	splitter->setSizes({400, 450});
	const auto before_drag = splitter->sizes();
	QSignalSpy moved(splitter, &QSplitter::splitterMoved);
	dragSplitter(splitter, 60);
	require(!moved.isEmpty() && splitter->sizes()[0] > before_drag[0] && splitter->sizes()[1] < before_drag[1], "drag down must enlarge preview");
	const auto after_drag = splitter->sizes();
	dragSplitter(splitter, -80);
	require(splitter->sizes()[0] < after_drag[0] && splitter->sizes()[1] > after_drag[1], "drag up must enlarge library");
	splitter->setSizes({10000, 0});
	require(library->height() >= 280 && table->height() >= 100, "library minimum size");
	splitter->setSizes({0, 10000});
	require(preview->height() >= 200, "preview minimum size");
	splitter->setSizes({400, 450});
	for(auto* widget : panel.findChildren<QWidget*>())
		if(qobject_cast<QAbstractButton*>(widget) || qobject_cast<QLineEdit*>(widget) ||
			qobject_cast<QAbstractSlider*>(widget) || qobject_cast<QDoubleSpinBox*>(widget))
			requireRussianToolTip(widget);
	auto* help = panel.findChild<QLabel*>("animationImportHelp");
	require(help && help->text().contains("FBX / GLB / SUBANIM") && !help->text().contains("Blender"), "native import format help");
	QSignalSpy selection(&panel, &AnimationEditorPanel::animationSelected);
	QSignalSpy playback(&panel, &AnimationEditorPanel::previewPlaybackChanged);
	QSignalSpy seek(&panel, &AnimationEditorPanel::previewSeekRequested);
	QSignalSpy flags(&panel, &AnimationEditorPanel::flagsEdited);
	panel.setAnimations({{"first.subanim", "First", 2, true, false}, {"second.subanim", "Second", 5, false, true}});
	require(selection.count() == 1 && panel.selectedAnimationId() == "first.subanim");
	require(table->item(0, 0)->toolTip().contains(QStringLiteral("Выбрать жест")) && table->item(0, 0)->toolTip().contains("first.subanim"), "Russian row tooltip preserves resource ID");
	for(int column = 0; column < table->columnCount(); ++column)
		require(QRegularExpression(QStringLiteral("[А-Яа-яЁё]")).match(table->horizontalHeaderItem(column)->toolTip()).hasMatch(), "Russian table header tooltip");
	search->setText("second");
	require(table->isRowHidden(0) && !table->isRowHidden(1), "search still filters library");
	auto* clear = search->findChild<QToolButton*>();
	requireRussianToolTip(clear);
	clear->click();
	require(search->text().isEmpty() && !table->isRowHidden(0), "search clear button");
	panel.setPreviewDuration(2);
	auto* play = panel.findChild<QPushButton*>("animationPlay");
	const QString play_tip = play->toolTip();
	play->click(); require(playback.last()[0].toBool());
	require(play->toolTip() != play_tip && play->toolTip().contains(QStringLiteral("Приостановить")), "pause tooltip follows playback");
	panel.setPreviewPlaying(false); require(playback.count() == 2); // selection pause + click; no feedback signal
	require(play->toolTip() == play_tip, "play tooltip restored");
	auto* timeline = panel.findChild<QSlider*>("animationTimeline");
	panel.setPreviewPosition(.4); require(seek.isEmpty());
	timeline->setValue(5000); require(seek.count() == 1 && seek.last()[0].toDouble() == .5);
	panel.setEditingEnabled(true);
	panel.findChild<QCheckBox*>("animationLoop")->click(); require(flags.count() == 1);
	panel.setAnimations({{"first.subanim", "First", 2, false, false}, {"second.subanim", "Second", 5, false, true}}, "second.subanim");
	require(panel.selectedAnimationId() == "second.subanim" && !play->isChecked());
	panel.setPreviewDuration(5);
	panel.setBusy(true); require(!play->isEnabled() && !panel.findChild<QPushButton*>("animationImport")->isEnabled());
	panel.setBusy(false); require(play->isEnabled());
	if(argc > 1)
	{
		panel.resize(480, 900);
		panel.show(); app.processEvents();
		panel.grab().save(QString::fromLocal8Bit(argv[1]));
	}
	panel.setAnimations({}); require(panel.selectedAnimationId().isEmpty() && !play->isEnabled());
	std::cout << "PASS: splitter dragging/minimum sizes, Russian control tooltips, native import help, search, real selection, transport signals, seek without feedback, flags, busy/empty states\n";
	return 0;
}
catch(const std::exception& e)
{
	std::cerr << "FAIL: " << e.what() << '\n';
	return 1;
}
